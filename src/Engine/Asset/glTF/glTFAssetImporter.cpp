#include "ayapch.h"
#include "glTFAssetImporter.hpp"

#include <cgltf/cgltf.h>

#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"
#include "Scene/Entity.hpp"
#include "Scene/SceneSerializer.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/MaterialSerializer.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Model.hpp"
#include "Asset/AssetManager.hpp"
#include "Asset/Prefab.hpp"
#include "Project/Project.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Core/VFS.hpp"

#include <fstream>
#include <yaml-cpp/yaml.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize2.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Ayaya {

// ==========================================
// URI URL-Decode
// ==========================================
static std::string UrlDecode(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); i++) {
        if (src[i] == '%' && i + 2 < src.size() && isxdigit(src[i+1]) && isxdigit(src[i+2])) {
            int hi = src[i+1] <= '9' ? src[i+1]-'0' : (src[i+1]|32)-'a'+10;
            int lo = src[i+2] <= '9' ? src[i+2]-'0' : (src[i+2]|32)-'a'+10;
            out += (char)((hi << 4) | lo);
            i += 2;
        } else if (src[i] == '+') {
            out += ' ';
        } else {
            out += src[i];
        }
    }
    return out;
}

// ==========================================
// Name deduplication
// ==========================================
static std::string DeduplicatePath(const std::string& path) {
    if (!std::filesystem::exists(path)) return path;
    auto parent = std::filesystem::path(path).parent_path();
    auto stem   = std::filesystem::path(path).stem().string();
    auto ext    = std::filesystem::path(path).extension().string();
    for (int i = 1; ; i++) {
        std::string candidate = (parent / (stem + "_" + std::to_string(i) + ext)).string();
        if (!std::filesystem::exists(candidate)) return candidate;
    }
}

// ==========================================
// Try to read existing UUID from .meta file.
// Returns 0 if .meta doesn't exist or is corrupt.
// ==========================================
static UUID TryReadExistingUUID(const std::string& physicalPath) {
    std::string metaPath = physicalPath + ".meta";
    if (!std::filesystem::exists(metaPath)) return 0;
    UUID handle; AssetType type;
    if (AssetManager::ReadMetaFile(metaPath, handle, type))
        return handle;
    return 0;
}

// ==========================================
// glTF Mat4 → GLM (column-major from cgltf)
// ==========================================
static glm::mat4 GltfNodeToMat4(const cgltf_node* node) {
    if (node->has_matrix) return glm::make_mat4(&node->matrix[0]);
    glm::mat4 m(1.0f);
    if (node->has_translation)
        m = glm::translate(m, glm::vec3(node->translation[0], node->translation[1], node->translation[2]));
    if (node->has_rotation)
        m *= glm::mat4_cast(glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]));
    if (node->has_scale)
        m = glm::scale(m, glm::vec3(node->scale[0], node->scale[1], node->scale[2]));
    return m;
}

// ==========================================
// Extract glTF primitive → engine Vertex buffer
// ==========================================
static std::shared_ptr<Mesh> ExtractPrimitive(const cgltf_primitive& prim, int matIdx) {
    const cgltf_accessor* posAcc = nullptr, *nrmAcc = nullptr, *uvAcc = nullptr, *idxAcc = prim.indices;
    for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
        switch (prim.attributes[a].type) {
            case cgltf_attribute_type_position: posAcc = prim.attributes[a].data; break;
            case cgltf_attribute_type_normal:   nrmAcc = prim.attributes[a].data; break;
            case cgltf_attribute_type_texcoord: uvAcc  = prim.attributes[a].data; break;
            default: break;
        }
    }
    if (!posAcc || posAcc->type != cgltf_type_vec3) return nullptr;

    std::vector<Vertex> vertices(posAcc->count);
    for (cgltf_size i = 0; i < posAcc->count; ++i) {
        float p[3]={0}, n[3]={0}, u[2]={0};
        cgltf_accessor_read_float(posAcc, i, p, 3);
        if (nrmAcc) cgltf_accessor_read_float(nrmAcc, i, n, 3);
        if (uvAcc)  cgltf_accessor_read_float(uvAcc,  i, u, 2);
        vertices[i].Position = glm::vec3(p[0], p[1], p[2]);
        vertices[i].Normal   = glm::vec3(n[0], n[1], n[2]);
        vertices[i].TexCoord = glm::vec2(u[0], u[1]);
    }

    std::vector<uint32_t> indices;
    if (idxAcc) {
        indices.resize(idxAcc->count);
        for (cgltf_size i = 0; i < idxAcc->count; ++i)
            indices[i] = (uint32_t)cgltf_accessor_read_index(idxAcc, i);
    } else {
        indices.resize(posAcc->count);
        for (cgltf_size i = 0; i < posAcc->count; ++i) indices[i] = (uint32_t)i;
    }
    // Vulkan GDR path: skip per-mesh VBO/IBO — GeometryPool SSBO serves vertex data
    bool createBuffers = (RendererAPI::GetAPI() == RendererAPI::API::OpenGL);
    return std::make_shared<Mesh>(vertices, indices, matIdx, createBuffers);
}

// ==========================================
// Convert glTF PBR material → engine Material
// ==========================================
static std::shared_ptr<Material> ConvertMaterial(const cgltf_material* mat,
                                                   std::unordered_map<std::string, UUID>& texCache) {
    if (!mat) return nullptr;
    // Clone from DefaultPBR to inherit all standard properties (u_Alpha, u_Emissive, etc.)
    UUID builtIn = AssetManager::GetBuiltInMaterial();
    auto defaultMat = AssetManager::GetAsset<Material>(builtIn);
    auto m = defaultMat ? std::make_shared<Material>(*defaultMat) : std::make_shared<Material>();
    m->Name = mat->name && mat->name[0] ? mat->name : "glTF Material";
    m->ShaderName = "PBR";

    if (mat->has_pbr_metallic_roughness) {
        auto& p = mat->pbr_metallic_roughness;
        m->SetVec4("u_Albedo", glm::vec4(p.base_color_factor[0], p.base_color_factor[1], p.base_color_factor[2], p.base_color_factor[3]));
        m->SetFloat("u_Metallic", p.metallic_factor);
        m->SetFloat("u_Roughness", p.roughness_factor);
        m->SetFloat("u_AO", 1.0f);
        if (p.base_color_texture.texture) {
            auto it = texCache.find(p.base_color_texture.texture->image->uri ? p.base_color_texture.texture->image->uri : "");
            if (it != texCache.end()) m->SetTexture("u_AlbedoMap", it->second);
        }
        if (p.metallic_roughness_texture.texture) {
            auto it = texCache.find(p.metallic_roughness_texture.texture->image->uri ? p.metallic_roughness_texture.texture->image->uri : "");
            if (it != texCache.end()) m->SetTexture("u_ORMMap", it->second);
        }
    }
    if (mat->normal_texture.texture) {
        auto it = texCache.find(mat->normal_texture.texture->image->uri ? mat->normal_texture.texture->image->uri : "");
        if (it != texCache.end()) m->SetTexture("u_NormalMap", it->second);
    }
    if (mat->occlusion_texture.texture) {
        auto it = texCache.find(mat->occlusion_texture.texture->image->uri ? mat->occlusion_texture.texture->image->uri : "");
        if (it != texCache.end()) m->SetTexture("u_AOMap", it->second);
    }
    if (mat->emissive_texture.texture) {
        auto it = texCache.find(mat->emissive_texture.texture->image->uri ? mat->emissive_texture.texture->image->uri : "");
        if (it != texCache.end()) m->SetTexture("u_EmissiveMap", it->second);
        m->SetVec3("u_Emissive", glm::vec3(mat->emissive_factor[0], mat->emissive_factor[1], mat->emissive_factor[2]));
    }
    switch (mat->alpha_mode) {
        case cgltf_alpha_mode_mask:  m->SetBlendMode(MaterialBlendMode::Masked); m->SetFloat("u_Alpha", mat->alpha_cutoff); break;
        case cgltf_alpha_mode_blend: m->SetBlendMode(MaterialBlendMode::Translucent); break;
        default: m->SetBlendMode(MaterialBlendMode::Opaque); break;
    }
    m->SetPacking(Material::TexturePacking::glTF_MetalRough);
    return m;
}

// ==========================================
// Build Prefab entity tree from glTF nodes
// ==========================================
static void BuildPrefabEntity(const cgltf_node* node, Entity parent, Scene& prefabScene,
                               const std::vector<glTFImportResult::MeshEntry>& meshEntries,
                               const std::vector<glTFImportResult::MatEntry>& matEntries,
                               const cgltf_data* data,
                               const glTFImportSettings& settings,
                               const std::unordered_map<std::string, int>& meshPrimToLinear) {
    // Skip target-only nodes. Respect import settings: if lights/cameras
    // are disabled, a light-only/camera-only node counts as empty.
    bool hasLight  = node->light && settings.ImportLights;
    bool hasCamera = node->camera && settings.ImportCameras;
    bool hasContent = node->mesh || hasLight || hasCamera || node->children_count > 0;
    if (!hasContent) return;

    Entity e = prefabScene.CreateEntity(node->name && node->name[0] ? node->name : "Node");

    glm::mat4 local = GltfNodeToMat4(node);
    glm::vec3 scale, trans, skew; glm::vec4 persp; glm::quat rot;
    glm::decompose(local, scale, rot, trans, skew, persp);
    auto& tc = e.GetComponent<TransformComponent>();
    tc.Translation = trans; tc.Rotation = glm::eulerAngles(rot); tc.Scale = scale;

    if (parent) e.SetParent(parent, false);  // glTF local transforms are already relative to parent

    // Mesh
    if (node->mesh) {
        int meshIdx = (int)(node->mesh - data->meshes);
        bool multi = node->mesh->primitives_count > 1;
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            Entity target = multi ? prefabScene.CreateEntity(
                std::string(node->name && node->name[0] ? node->name : "prim") + "_" + std::to_string(p)) : e;
            if (multi) target.SetParent(e, false);  // glTF local transforms already relative

            auto& mr = target.AddComponent<MeshRendererComponent>();
            // Look up SubMesh by linear index from the meshPrimToLinear map
            std::string key = std::to_string(meshIdx) + ":" + std::to_string(p);
            auto lit = meshPrimToLinear.find(key);
            if (lit != meshPrimToLinear.end() && lit->second < (int)meshEntries.size()) {
                mr.ModelHandle = meshEntries[lit->second].Handle;
            } else {
                AYAYA_CORE_WARN("glTF prefab: no SubMesh match for node '{}' mesh[{}] prim[{}] (meshEntries={})",
                    node->name ? node->name : "(unnamed)", meshIdx, (int)p, meshEntries.size());
            }
            // Match material: primitive material index
            int matIdx = (int)p < (int)data->materials_count ? (int)p : -1;
            if (matIdx >= 0 && matIdx < (int)matEntries.size()) {
                mr.MaterialHandle = matEntries[matIdx].Handle;
            }
        }
    }

    // Light (respect import settings)
    if (node->light && settings.ImportLights) {
        switch (node->light->type) {
            case cgltf_light_type_directional: {
                auto& l = e.AddComponent<DirectionalLightComponent>();
                l.Color = glm::vec3(node->light->color[0], node->light->color[1], node->light->color[2]);
                l.Illuminance = node->light->intensity; break;
            }
            case cgltf_light_type_point: {
                auto& l = e.AddComponent<PointLightComponent>();
                l.Color = glm::vec3(node->light->color[0], node->light->color[1], node->light->color[2]);
                l.LuminousPower = node->light->intensity;
                l.Radius = node->light->range > 0 ? node->light->range : 10.0f; break;
            }
            case cgltf_light_type_spot: {
                auto& sl = e.AddComponent<SpotLightComponent>();
                sl.Color = glm::vec3(node->light->color[0], node->light->color[1], node->light->color[2]);
                sl.LuminousPower = node->light->intensity;
                sl.Radius = node->light->range > 0 ? node->light->range : 10.0f;
                sl.InnerConeAngle = node->light->spot_inner_cone_angle;
                sl.OuterConeAngle = node->light->spot_outer_cone_angle; break;
            }
            default: break;
        }
    }

    // Camera (respect import settings)
    if (node->camera && settings.ImportCameras && node->camera->type == cgltf_camera_type_perspective) {
        auto& cam = e.AddComponent<CameraComponent>();
        cam.Camera.SetPerspective(node->camera->data.perspective.yfov,
            node->camera->data.perspective.znear,
            node->camera->data.perspective.zfar > 0 ? node->camera->data.perspective.zfar : 1000.0f);
    }

    // EnTT-safe: copy children before recursion
    std::vector<cgltf_node*> kids(node->children, node->children + node->children_count);
    for (auto* c : kids) BuildPrefabEntity(c, e, prefabScene, meshEntries, matEntries, data, settings, meshPrimToLinear);
}

// ==========================================
// Phase 1: Background-thread import
// ==========================================
glTFImportResult ImportglTFSceneSync(const std::string& sourcePath,
                                     const glTFImportSettings& settings) {
    glTFImportResult result;

    // === 1. Parse ===
    cgltf_options opts{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, sourcePath.c_str(), &data) != cgltf_result_success) {
        result.ErrorMsg = "cgltf_parse_file failed"; return result;
    }
    if (cgltf_load_buffers(&opts, data, sourcePath.c_str()) != cgltf_result_success) {
        result.ErrorMsg = "cgltf_load_buffers failed"; cgltf_free(data); return result;
    }

    AYAYA_CORE_INFO("glTF import: {} nodes, {} meshes, {} materials, {} textures, {} lights",
        data->nodes_count, data->meshes_count, data->materials_count,
        data->textures_count, data->lights_count);

    // === 2. Asset directory setup ===
    std::string assetDir = Project::GetAssetDirectory().string();
    std::string baseName   = std::filesystem::path(sourcePath).stem().string();
    std::string destDir    = assetDir + "/" + baseName;
    std::filesystem::create_directories(destDir + "/textures");
    std::filesystem::create_directories(destDir + "/Materials");

    // === 3. Copy source .gltf/.glb ===
    std::string gltfExt  = std::filesystem::path(sourcePath).extension().string();
    // Detect re-import: if a .meta already exists at the base destination,
    // we're re-importing → overwrite in place, reuse UUIDs from .meta.
    // If no .meta → new import → deduplicate to avoid overwriting other files.
    bool isReimport = std::filesystem::exists(destDir + "/" + baseName + gltfExt + ".meta");
    std::string gltfDest;
    if (isReimport)
        gltfDest = destDir + "/" + baseName + gltfExt;
    else
        gltfDest = DeduplicatePath(destDir + "/" + baseName + gltfExt);
    std::filesystem::copy_file(sourcePath, gltfDest, std::filesystem::copy_options::overwrite_existing);
    result.ModelPhysicalPath = gltfDest;
    result.ModelVirtualPath  = VFS::GetVirtualPath(gltfDest);
    result.ModelHandle       = TryReadExistingUUID(gltfDest);
    if (result.ModelHandle == 0) result.ModelHandle = UUID();

    // === 4. Copy external .bin ===
    for (cgltf_size i = 0; i < data->buffers_count; i++) {
        if (data->buffers[i].uri && !strstr(data->buffers[i].uri, "data:")) {
            auto glTFDir = std::filesystem::path(sourcePath).parent_path();
            auto binSrc = std::filesystem::weakly_canonical(glTFDir / UrlDecode(data->buffers[i].uri));
            if (std::filesystem::exists(binSrc)) {
                std::string binDest = destDir + "/" + std::filesystem::path(binSrc).filename().string();
                if (!isReimport) binDest = DeduplicatePath(binDest);
                std::filesystem::copy_file(binSrc, binDest, std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

    // === 5. Build sRGB texture index set ===
    std::unordered_set<int> srgbIndices;
    for (cgltf_size i = 0; i < data->materials_count; i++) {
        auto& m = data->materials[i];
        if (auto* t = m.pbr_metallic_roughness.base_color_texture.texture) {
            for (cgltf_size j = 0; j < data->textures_count; j++)
                if (&data->textures[j] == t) { srgbIndices.insert((int)j); break; }
        }
        if (auto* t = m.emissive_texture.texture) {
            for (cgltf_size j = 0; j < data->textures_count; j++)
                if (&data->textures[j] == t) { srgbIndices.insert((int)j); break; }
        }
    }

    // === 6. Extract textures ===
    // FIXME: skip texture import for mesh-only debugging
#define SKIP_TEXTURE_IMPORT 0
    std::unordered_map<std::string, UUID> texUUIDs; // URI→UUID cache (dedup)
    for (cgltf_size i = 0; i < data->textures_count; i++) {
#if SKIP_TEXTURE_IMPORT
        continue;
#endif
        auto& img = *data->textures[i].image;
        std::string texName = baseName + "_Tex" + std::to_string(i);
        std::string texDest;

        if (img.uri && !strstr(img.uri, "data:")) {
            // External: URL-decode, resolve relative path (handles ../ etc.)
            std::string decoded = UrlDecode(img.uri);
            auto glTFDir = std::filesystem::path(sourcePath).parent_path();
            auto srcPath = std::filesystem::weakly_canonical(glTFDir / decoded);
            auto srcFn = srcPath.filename().string();
            if (isReimport)
                texDest = destDir + "/textures/" + srcFn;
            else
                texDest = DeduplicatePath(destDir + "/textures/" + srcFn);
            if (std::filesystem::exists(srcPath))
                std::filesystem::copy_file(srcPath, texDest, std::filesystem::copy_options::overwrite_existing);
        } else if (img.buffer_view) {
            // Embedded: dump raw bytes (already PNG/JPEG)
            const char* ext = ".png";
            if (strstr(img.mime_type, "jpeg") || strstr(img.mime_type, "jpg")) ext = ".jpg";
            texName += ext;
            if (isReimport)
                texDest = destDir + "/textures/" + texName;
            else
                texDest = DeduplicatePath(destDir + "/textures/" + texName);
            auto& bv = *img.buffer_view;
            const uint8_t* src = (const uint8_t*)bv.buffer->data + bv.offset;
            std::ofstream(texDest, std::ios::binary).write((const char*)src, bv.size);
        } else continue;

        if (texDest.empty()) continue;

        // Downscale textures > 2048 to prevent OOM (4K RGBA = 64MB → 2K = 16MB, 4x savings)
        {
            int w, h, comp;
            if (stbi_info(texDest.c_str(), &w, &h, &comp) && (w > 2048 || h > 2048)) {
                int newW = w, newH = h;
                if (w >= h)    { newW = 2048; newH = (int)((float)h * 2048.0f / w); if (newH < 1) newH = 1; }
                else           { newH = 2048; newW = (int)((float)w * 2048.0f / h); if (newW < 1) newW = 1; }

                uint8_t* src = stbi_load(texDest.c_str(), &w, &h, &comp, 4);
                if (src) {
                    std::vector<uint8_t> dst(newW * newH * 4);
                    bool isSRGB = srgbIndices.count((int)i) > 0;
                    if (isSRGB)
                        stbir_resize_uint8_srgb(src, w, h, 0, dst.data(), newW, newH, 0, STBIR_RGBA);
                    else
                        stbir_resize_uint8_linear(src, w, h, 0, dst.data(), newW, newH, 0, STBIR_RGBA);
                    stbi_write_png(texDest.c_str(), newW, newH, 4, dst.data(), newW * 4);
                    AYAYA_CORE_INFO("glTF import: resized texture {} from {}x{} → {}x{}",
                        texDest, w, h, newW, newH);
                    stbi_image_free(src);
                }
            }
        }

        // Record for Phase 2 finalization (sRGB flag carried via .meta update)
        UUID texUUID = TryReadExistingUUID(texDest);
        if (texUUID == 0) texUUID = UUID();
        TextureImportSettings tSet;
        tSet.SRGB = srgbIndices.count((int)i) > 0;
        tSet.FlipY = false;
        tSet.GenerateMipmaps = settings.GenerateMipmaps;
        if (settings.GenerateMipmaps && data->textures_count > 8) {
            AYAYA_CORE_WARN("glTF import: {} textures with mipmaps enabled — high memory usage risk",
                data->textures_count);
        }
        AssetManager::WriteMetaFile(texDest, texUUID, AssetType::Texture2D, tSet);
        result.CopiedTextures.push_back({texDest, texUUID});
        result.CopiedTextures.back().SRGB = srgbIndices.count((int)i) > 0;
        texUUIDs[img.uri ? img.uri : texName] = texUUID;
    }

    // === 7. Build materials ===
    for (cgltf_size i = 0; i < data->materials_count; i++) {
        auto mat = ConvertMaterial(&data->materials[i], texUUIDs);
        if (!mat) continue;

        std::string matName = mat->Name.empty() ? "Material_" + std::to_string(i) : mat->Name;
        // Sanitize filename
        for (auto& c : matName) if (c == '/' || c == '\\' || c == ':') c = '_';
        std::string matPath;
        if (isReimport)
            matPath = destDir + "/Materials/" + matName + ".mat";
        else
            matPath = DeduplicatePath(destDir + "/Materials/" + matName + ".mat");
        MaterialSerializer::Serialize(mat, matPath);

        UUID matUUID = TryReadExistingUUID(matPath);
        if (matUUID == 0) matUUID = UUID();
        AssetManager::WriteMetaFile(matPath, matUUID, AssetType::Material);
        result.Materials.push_back({matUUID, VFS::GetVirtualPath(matPath), matPath});
    }

    // === 8. Build SubMeshes ===
    // On re-import, read parent model's .meta to recover existing SubMesh UUIDs
    // keyed by sub_mesh_index (the linear index matches the previous import).
    std::unordered_map<int, UUID> reusedSubMeshUUIDs;
    if (isReimport) {
        std::string modelMetaPath = gltfDest + ".meta";
        if (std::filesystem::exists(modelMetaPath)) {
            try {
                YAML::Node metaYaml = YAML::LoadFile(modelMetaPath);
                if (metaYaml["sub_assets"]) {
                    for (auto sub : metaYaml["sub_assets"]) {
                        if (sub["uuid"] && sub["sub_mesh_index"].IsDefined()) {
                            reusedSubMeshUUIDs[sub["sub_mesh_index"].as<int>()] = UUID(sub["uuid"].as<uint64_t>());
                        }
                    }
                }
            } catch (...) {} // .meta parse failure → fall through, generate new UUIDs
        }
    }

    // Use linear index (0,1,2...) for SubMeshIndex — maps directly to cgltf parse order.
    // The meshPrimToLinear map is used by BuildPrefabEntity to resolve (meshIdx,primIdx) → index.
    int linearIdx = 0;
    std::unordered_map<std::string, int> meshPrimToLinear;
    for (cgltf_size i = 0; i < data->meshes_count; i++) {
        for (cgltf_size p = 0; p < data->meshes[i].primitives_count; p++) {
            auto mesh = ExtractPrimitive(data->meshes[i].primitives[p],
                (int)p < (int)data->materials_count ? (int)p : -1);
            if (!mesh) continue;

            auto it = reusedSubMeshUUIDs.find(linearIdx);
            UUID subUUID = (it != reusedSubMeshUUIDs.end()) ? it->second : UUID();
            std::string subName = baseName + "_Mesh" + std::to_string(i) + "_" + std::to_string(p);
            std::string key = std::to_string(i) + ":" + std::to_string(p);
            meshPrimToLinear[key] = linearIdx;
            result.SubMeshes.push_back({subUUID, result.ModelVirtualPath, "", linearIdx, subName});
            result.SubMeshData.push_back(mesh);
            linearIdx++;
        }
    }

    // === 9. Build Prefab ===
    auto prefab = std::make_shared<Prefab>();
    Scene* prefabScene = prefab->GetScene();
    Entity rootEntity = prefabScene->CreateEntity(baseName);
    prefab->SetRootEntity(rootEntity);
    cgltf_scene* scene = data->scene ? data->scene : &data->scenes[0];
    for (cgltf_size i = 0; i < scene->nodes_count; i++) {
        BuildPrefabEntity(scene->nodes[i], rootEntity, *prefabScene,
            result.SubMeshes, result.Materials, data, settings, meshPrimToLinear);
    }

    if (isReimport)
        result.PrefabPath = destDir + "/" + baseName + ".prefab";
    else
        result.PrefabPath = DeduplicatePath(destDir + "/" + baseName + ".prefab");
    prefab->Save(result.PrefabPath);
    result.PrefabHandle = TryReadExistingUUID(result.PrefabPath);
    if (result.PrefabHandle == 0) result.PrefabHandle = UUID();
    AssetManager::WriteMetaFile(result.PrefabPath, result.PrefabHandle, AssetType::Prefab);

    result.Success = true;
    result.NodeCount = (int)data->nodes_count;
    result.MeshCount = (int)data->meshes_count;
    result.MaterialCount = (int)data->materials_count;
    result.TextureCount = (int)data->textures_count;
    result.LightCount = (int)data->lights_count;

    cgltf_free(data);
    return result;
}

// ==========================================
// Phase 2: Main-thread finalization
// ==========================================
void FinalizeglTFImport(glTFImportResult& result) {
    if (!result.Success) {
        AYAYA_CORE_ERROR("glTF import finalize failed: {}", result.ErrorMsg);
        return;
    }

    // Register model in registry (ImportAsset writes .meta + populates s_Registry)
    // Use the UUID returned by ImportAsset (may differ from result.ModelHandle if .meta existed)
    UUID modelUUID = AssetManager::ImportAsset(result.ModelPhysicalPath);

    // Register SubMesh assets (wrap extracted meshes in Model, persist in s_Registry)
    int subMeshCached = 0;
    for (size_t i = 0; i < result.SubMeshes.size() && i < result.SubMeshData.size(); ++i) {
        auto& sub = result.SubMeshes[i];
        auto& mesh = result.SubMeshData[i];
        if (mesh) {
            auto subModel = std::make_shared<Model>(mesh);
            AssetManager::AddAsset<Model>(sub.Handle, subModel);
            AssetManager::RegisterSubMesh(sub.Handle, modelUUID,
                sub.SubMeshIndex, result.ModelVirtualPath);
            subMeshCached++;
        }
    }
    // Verify: sample SubMesh mesh data (vertex/index counts)
    uint64_t totalVerts = 0, totalInds = 0;
    int emptyMeshes = 0;
    for (size_t vi = 0; vi < result.SubMeshData.size(); vi++) {
        if (result.SubMeshData[vi]) {
            totalVerts += result.SubMeshData[vi]->GetVertexCount();
            totalInds  += result.SubMeshData[vi]->GetIndexCount();
        } else {
            emptyMeshes++;
        }
    }
    // Log first 5 meshes for cross-reference with LoadglTFAsModel
    for (int si = 0; si < 5 && si < (int)result.SubMeshData.size(); si++) {
        auto& m = result.SubMeshData[si];
        if (m) AYAYA_CORE_INFO("glTF import: SubMesh[{}] v={} i={}", si, m->GetVertexCount(), m->GetIndexCount());
    }
    AYAYA_CORE_INFO("glTF import: {} SubMeshes, {} total verts, {} total inds, {} empty meshes",
        result.SubMeshData.size(), totalVerts, totalInds, emptyMeshes);
    if (emptyMeshes > 0)
        AYAYA_CORE_WARN("glTF import: {} SubMeshes have NULL mesh data!", emptyMeshes);
    AYAYA_CORE_TRACE("glTF import: cached {} SubMesh Models in asset pool (total entries: {})",
        subMeshCached, result.SubMeshes.size());

    // Register textures lazily, then trigger async preload.
    // RequestAsyncLoad spawns background threads for stbi_load, then queues main-thread
    // GPU uploads. By the time the user drags the prefab into the scene, textures are
    // already GPU-resident → GetAsset<Texture2D> returns cached instantly, no stutter.
    for (auto& t : result.CopiedTextures) {
        AssetManager::RegisterTextureAsset(t.Handle, t.PhysicalPath);
#if !SKIP_TEXTURE_IMPORT
        AssetManager::RequestAsyncLoad(t.Handle);
#endif
    }

    // Register materials
    for (auto& m : result.Materials) {
        AssetManager::ImportAsset(m.PhysicalPath);
    }

    // Register prefab
    AssetManager::ImportAsset(result.PrefabPath);

    // Drain GPU deferred-release queue after bulk import.
    // 115 meshes + 72 textures → staging buffers/VkImages accumulate in 3-frame queue → OOM.
    if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
        auto ctx = Application::Get().GetWindow().GetContext();
        if (auto vk = std::dynamic_pointer_cast<VulkanContext>(ctx)) {
            vkDeviceWaitIdle(vk->GetDevice());
            vk->ProcessDeferredResources(true);

            // Log VMA memory budget for diagnostics
            VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
            vmaGetHeapBudgets(vk->GetAllocator(), budgets);
            for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; ++i) {
                if (budgets[i].budget > 0) {
                    AYAYA_CORE_INFO("glTF import: VMA heap[{}] usage={:.1f}MB / budget={:.1f}MB",
                        i, budgets[i].usage / (1024.0 * 1024.0),
                        budgets[i].budget / (1024.0 * 1024.0));
                }
            }
        }
    }

    // Free CPU-side mesh data — vertex/index vectors no longer needed after GPU upload
    size_t freedBytes = 0;
    for (auto& meshPtr : result.SubMeshData) {
        if (meshPtr) {
            freedBytes += meshPtr->GetVertexCount() * sizeof(Vertex)
                        + meshPtr->GetIndexCount() * sizeof(uint32_t);
        }
    }
    result.SubMeshData.clear();
    if (freedBytes > 0)
        AYAYA_CORE_TRACE("glTF import: freed {:.1f} MB CPU geometry data", freedBytes / (1024.0 * 1024.0));

    AYAYA_CORE_INFO("glTF import complete: {} nodes, {} meshes, {} materials, {} textures, {} lights",
        result.NodeCount, result.MeshCount, result.MaterialCount, result.TextureCount, result.LightCount);

    // Re-enable AssetWatcher — all files are registered, watcher can safely process future changes
    AssetManager::SetBulkImportInProgress(false);
}

// ==========================================
// Standalone glTF loader — parses with cgltf, returns Model with ALL meshes
// in cgltf parse order (matching SubMeshIndex from import).
// ==========================================
std::shared_ptr<Model> LoadglTFAsModel(const std::string& filePath) {
    std::string ext = std::filesystem::path(filePath).extension().string();
    for (auto& c : ext) c = (char)std::tolower(c);
    if (ext != ".gltf" && ext != ".glb") return nullptr;

    cgltf_options opts{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, filePath.c_str(), &data) != cgltf_result_success)
        return nullptr;
    if (cgltf_load_buffers(&opts, data, filePath.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return nullptr;
    }

    // Extract all primitives in cgltf order → matches SubMeshIndex encoding
    std::vector<std::shared_ptr<Mesh>> allMeshes;
    for (cgltf_size i = 0; i < data->meshes_count; i++) {
        for (cgltf_size p = 0; p < data->meshes[i].primitives_count; p++) {
            auto mesh = ExtractPrimitive(data->meshes[i].primitives[p],
                (int)p < (int)data->materials_count ? (int)p : -1);
            if (mesh) allMeshes.push_back(mesh);
        }
    }
    cgltf_free(data);

    uint64_t loadTotalVerts = 0, loadTotalInds = 0;
    int nonzeroMeshCount = 0;
    for (auto& m : allMeshes) {
        loadTotalVerts += m->GetVertexCount();
        loadTotalInds  += m->GetIndexCount();
        if (m->GetVertexCount() > 0 && m->GetIndexCount() > 0) nonzeroMeshCount++;
    }
    AYAYA_CORE_INFO("LoadglTFAsModel: {} meshes ({} nonzero), {} verts, {} inds from {}",
        allMeshes.size(), loadTotalVerts, loadTotalInds, filePath);

    if (allMeshes.empty()) return nullptr;

    // Model has no default ctor — use first mesh to init, then add rest
    auto model = std::make_shared<Model>(allMeshes[0]);
    for (size_t i = 1; i < allMeshes.size(); i++) {
        model->GetRootNode().Meshes.push_back(allMeshes[i]);
        const_cast<std::vector<std::shared_ptr<Mesh>>&>(model->GetMeshes()).push_back(allMeshes[i]);
    }
    return model;
}

} // namespace Ayaya
