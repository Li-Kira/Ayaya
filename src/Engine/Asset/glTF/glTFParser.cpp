#include "ayapch.h"
#include "glTFParser.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"
#include "Scene/Entity.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Model.hpp"
#include "Asset/AssetManager.hpp"
#include "Core/Log.hpp"
#include "Core/UUID.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Ayaya {

// ==========================================
// Helpers
// ==========================================

static glm::mat4 CgltfNodeToMat4(const cgltf_node* node) {
    if (node->has_matrix) {
        return glm::make_mat4(&node->matrix[0]);
    }
    glm::mat4 m(1.0f);
    if (node->has_translation)
        m = glm::translate(m, glm::vec3(node->translation[0], node->translation[1], node->translation[2]));
    if (node->has_rotation)
        m *= glm::mat4_cast(glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]));
    if (node->has_scale)
        m = glm::scale(m, glm::vec3(node->scale[0], node->scale[1], node->scale[2]));
    return m;
}

static glm::vec4 CgltfFloat4ToGlm(const cgltf_float* f) {
    return glm::vec4(f[0], f[1], f[2], f[3]);
}

// ==========================================
// Mesh Extraction
// ==========================================

static std::shared_ptr<Mesh> ExtractPrimitiveMesh(const cgltf_primitive& prim, int materialIndex) {
    const cgltf_accessor* posAcc = nullptr;
    const cgltf_accessor* nrmAcc = nullptr;
    const cgltf_accessor* uvAcc  = nullptr;
    const cgltf_accessor* idxAcc = prim.indices;

    for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
        const auto& attr = prim.attributes[a];
        switch (attr.type) {
            case cgltf_attribute_type_position: posAcc = attr.data; break;
            case cgltf_attribute_type_normal:   nrmAcc = attr.data; break;
            case cgltf_attribute_type_texcoord: uvAcc  = attr.data; break;
            default: break;
        }
    }
    if (!posAcc || posAcc->type != cgltf_type_vec3) return nullptr;

    const cgltf_size vtxCount = posAcc->count;
    std::vector<Vertex> vertices(vtxCount);

    const auto ReadF3 = [](const cgltf_accessor* a, cgltf_size i, float def[3]) {
        if (!a || i >= a->count) return;
        cgltf_accessor_read_float(a, i, def, 3);
    };
    const auto ReadF2 = [](const cgltf_accessor* a, cgltf_size i, float def[2]) {
        if (!a || i >= a->count) return;
        cgltf_accessor_read_float(a, i, def, 2);
    };

    for (cgltf_size i = 0; i < vtxCount; ++i) {
        float p[3]={0,0,0}, n[3]={0,0,0}, u[2]={0,0};
        ReadF3(posAcc, i, p);
        ReadF3(nrmAcc, i, n);
        ReadF2(uvAcc,  i, u);
        vertices[i].Position = glm::vec3(p[0], p[1], p[2]);
        vertices[i].Normal   = glm::vec3(n[0], n[1], n[2]);
        vertices[i].TexCoord = glm::vec2(u[0], u[1]);
        vertices[i].Tangent  = glm::vec3(0.0f); // will be computed in shader via dFdx/dFdy
    }

    // Indices
    std::vector<uint32_t> indices;
    if (idxAcc && idxAcc->count > 0) {
        indices.resize(idxAcc->count);
        for (cgltf_size i = 0; i < idxAcc->count; ++i)
            indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(idxAcc, i));
    } else {
        indices.resize(vtxCount);
        for (cgltf_size i = 0; i < vtxCount; ++i) indices[i] = static_cast<uint32_t>(i);
    }

    return std::make_shared<Mesh>(vertices, indices, materialIndex);
}

// ==========================================
// Texture Loading (with dedup cache)
// ==========================================

static UUID LoadTextureCached(const cgltf_texture* tex, bool sRGB,
                               std::unordered_map<std::string, UUID>& cache) {
    if (!tex || !tex->image) return 0;
    const char* uri = tex->image->uri;
    std::string key;

    if (uri) {
        key = uri;
    } else if (tex->image->buffer_view) {
        // Embedded texture: use buffer pointer as key
        key = "embedded:" + std::to_string(reinterpret_cast<uintptr_t>(tex->image->buffer_view->buffer->data))
              + ":" + std::to_string(tex->image->buffer_view->offset);
    } else {
        return 0;
    }

    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    // TODO: actual texture loading from URI or embedded buffer
    // For now, return 0 (will be filled in Phase 2.4)
    UUID handle = 0;

    // RawTextureData raw;
    // if (uri) {
    //     raw = LoadRawTexture(uri, sRGB);
    // } else {
    //     raw = LoadRawTextureFromBuffer(tex->image->buffer_view, sRGB);
    // }
    // handle = am.CreateTexture(raw);

    cache[key] = handle;
    return handle;
}

// ==========================================
// PBR Material Conversion
// ==========================================

static std::shared_ptr<Material> ConvertMaterial(const cgltf_material* mat,
                                                   const std::string& modelDir,
                                                   std::unordered_map<std::string, UUID>& texCache) {
    if (!mat) return nullptr;

    auto engineMat = std::make_shared<Material>();
    engineMat->Name = mat->name && mat->name[0] ? mat->name : "glTF Material";
    engineMat->ShaderName = "PBR";

    // === PBR Metallic-Roughness ===
    if (mat->has_pbr_metallic_roughness) {
        const auto& pbr = mat->pbr_metallic_roughness;
        engineMat->SetVec4("u_Albedo",   glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]));
        engineMat->SetFloat("u_Metallic",  pbr.metallic_factor);
        engineMat->SetFloat("u_Roughness", pbr.roughness_factor);
        engineMat->SetFloat("u_AO",        1.0f);

        // BaseColor Texture (sRGB)
        if (pbr.base_color_texture.texture) {
            UUID h = LoadTextureCached(pbr.base_color_texture.texture, true, texCache);
            if (h) engineMat->SetTexture("u_AlbedoMap", h);
        }

        // MetallicRoughness Texture (Linear)
        // glTF layout: B=Metallic, G=Roughness — matches UE4 ORM G/B ordering.
        // Register as ORM so shader reads o.g=roughness, o.b=metallic.
        // R channel in glTF is undefined (spec says 1.0) — AO comes from separate occlusionTexture.
        if (pbr.metallic_roughness_texture.texture) {
            UUID h = LoadTextureCached(pbr.metallic_roughness_texture.texture, false, texCache);
            if (h) engineMat->SetTexture("u_ORMMap", h);
        }
    }

    // === Normal Texture (Linear) ===
    if (mat->normal_texture.texture) {
        UUID h = LoadTextureCached(mat->normal_texture.texture, false, texCache);
        if (h) engineMat->SetTexture("u_NormalMap", h);
    }

    // === Occlusion Texture (Linear, R channel = AO) ===
    if (mat->occlusion_texture.texture) {
        UUID h = LoadTextureCached(mat->occlusion_texture.texture, false, texCache);
        if (h) engineMat->SetTexture("u_AOMap", h);
    }

    // === Emissive ===
    if (mat->has_emissive_strength || (mat->emissive_texture.texture)) {
        engineMat->SetVec3("u_Emissive", glm::vec3(mat->emissive_factor[0], mat->emissive_factor[1], mat->emissive_factor[2]));
        if (mat->emissive_texture.texture) {
            UUID h = LoadTextureCached(mat->emissive_texture.texture, true, texCache);
            if (h) engineMat->SetTexture("u_EmissiveMap", h);
        }
    }

    // === Alpha Mode ===
    switch (mat->alpha_mode) {
        case cgltf_alpha_mode_mask:
            engineMat->SetBlendMode(MaterialBlendMode::Masked);
            engineMat->SetFloat("u_Alpha", mat->alpha_cutoff);
            break;
        case cgltf_alpha_mode_blend:
            engineMat->SetBlendMode(MaterialBlendMode::Translucent);
            break;
        default:
            engineMat->SetBlendMode(MaterialBlendMode::Opaque);
            break;
    }

    // === Double Sided ===
    // (handled at mesh/pipeline level — see mesh rendering)

    engineMat->BakeProperties();
    return engineMat;
}

// ==========================================
// Light Parsing (KHR_lights_punctual)
// ==========================================

static void ParseLights(const cgltf_data* data, Scene& scene) {
    if (!data->lights_count) return;
    AYAYA_CORE_INFO("glTF: found {} KHR_lights_punctual lights", data->lights_count);

    // Lights are referenced by node extensions — handled during node traversal.
    // We store the light array for lookup by index.
    // Actual entity creation happens in ParseNode when a node has KHR_lights_punctual.
}

static void AttachLightToEntity(Entity entity, const cgltf_light* light) {
    switch (light->type) {
        case cgltf_light_type_directional: {
            auto& dl = entity.AddComponent<DirectionalLightComponent>();
            dl.Color       = glm::vec3(light->color[0], light->color[1], light->color[2]);
            dl.Illuminance = light->intensity;
            AYAYA_CORE_INFO("glTF: created DirectionalLight '{}' intensity={}", light->name ? light->name : "", light->intensity);
            break;
        }
        case cgltf_light_type_point: {
            auto& pl = entity.AddComponent<PointLightComponent>();
            pl.Color         = glm::vec3(light->color[0], light->color[1], light->color[2]);
            pl.LuminousPower = light->intensity;
            pl.Radius        = light->range > 0.0f ? light->range : 10.0f;
            AYAYA_CORE_INFO("glTF: created PointLight '{}' range={}", light->name ? light->name : "", pl.Radius);
            break;
        }
        case cgltf_light_type_spot: {
            // Fallback to point light + log spot info
            auto& pl = entity.AddComponent<PointLightComponent>();
            pl.Color         = glm::vec3(light->color[0], light->color[1], light->color[2]);
            pl.LuminousPower = light->intensity;
            pl.Radius        = light->range > 0.0f ? light->range : 10.0f;
            AYAYA_CORE_INFO("glTF: created SpotLight fallback as PointLight '{}' (spot angle not yet supported)", light->name ? light->name : "");
            break;
        }
        default: break;
    }
}

// ==========================================
// Node Hierarchy Traversal
// ==========================================

static void ParseNode(const cgltf_node* node, Entity parent, Scene& scene,
                      const cgltf_data* data,
                      std::vector<std::shared_ptr<Material>>& materials) {
    Entity entity = scene.CreateEntity(node->name && node->name[0] ? node->name : "glTF Node");

    // Build world transform
    glm::mat4 localTransform = CgltfNodeToMat4(node);
    glm::vec3 scale, translation, skew;
    glm::vec4 perspective;
    glm::quat rotation;
    glm::decompose(localTransform, scale, rotation, translation, skew, perspective);

    auto& tc = entity.GetComponent<TransformComponent>();
    tc.Translation = translation;
    tc.Rotation    = glm::eulerAngles(rotation);
    tc.Scale       = scale;

    // Hierarchy
    if (parent) {
        auto& rel = entity.GetComponent<RelationshipComponent>();
        entity.SetParent(parent);
    }

    // Mesh
    if (node->mesh) {
        auto& mrc = entity.AddComponent<MeshRendererComponent>();
        // For each primitive, create a sub-mesh entity
        bool multiPrim = (node->mesh->primitives_count > 1);

        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            const auto& prim = node->mesh->primitives[p];
            int matIdx = static_cast<int>(p < static_cast<cgltf_size>(materials.size()) ? p : -1);

            auto mesh = ExtractPrimitiveMesh(prim, matIdx);
            if (!mesh) continue;

            if (!multiPrim) {
                // Single primitive: attach directly to node entity
                auto model = std::make_shared<Model>(mesh);
                UUID modelUUID;
                AssetManager::AddAsset(modelUUID, model);
                mrc.ModelHandle = modelUUID;
                if (matIdx >= 0 && matIdx < static_cast<int>(materials.size()) && materials[matIdx]) {
                    UUID matUUID;
                    AssetManager::AddAsset(matUUID, materials[matIdx]);
                    mrc.MaterialHandle = matUUID;
                }
            } else {
                // Multiple primitives: create child entities
                Entity subEntity = scene.CreateEntity(
                    std::string(node->name ? node->name : "prim") + "_" + std::to_string(p));
                subEntity.SetParent(entity);

                auto& subMr = subEntity.AddComponent<MeshRendererComponent>();
                auto subModel = std::make_shared<Model>(mesh);
                UUID modelUUID;
                AssetManager::AddAsset(modelUUID, subModel);
                subMr.ModelHandle = modelUUID;
                if (matIdx >= 0 && matIdx < static_cast<int>(materials.size()) && materials[matIdx]) {
                    UUID matUUID;
                    AssetManager::AddAsset(matUUID, materials[matIdx]);
                    subMr.MaterialHandle = matUUID;
                }
            }
        }
    }

    // Camera
    if (node->camera) {
        auto& cam = entity.AddComponent<CameraComponent>();
        if (node->camera->type == cgltf_camera_type_perspective) {
            cam.Camera.SetPerspective(
                node->camera->data.perspective.yfov,
                node->camera->data.perspective.znear,
                node->camera->data.perspective.zfar > 0 ? node->camera->data.perspective.zfar : 1000.0f);
        }
    }

    // Light (KHR_lights_punctual)
    if (node->light) {
        AttachLightToEntity(entity, node->light);
    }

    // CRITICAL: Copy children list BEFORE recursion (EnTT reallocation safety)
    std::vector<cgltf_node*> children(node->children, node->children + node->children_count);
    for (auto* child : children) {
        ParseNode(child, entity, scene, data, materials);
    }
}

// ==========================================
// Root Correction Matrix
// ==========================================
// glTF: right-handed, Y-up, -Z forward.
// Engine: right-handed, Y-up, -Z forward (same convention).
// No coordinate-space correction needed — conventions match.

// ==========================================
// Main Import Function
// ==========================================

glTFImportResult glTFParser::ImportScene(const std::string& filePath,
                                          Scene& scene) {
    glTFImportResult result;

    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result parseResult = cgltf_parse_file(&options, filePath.c_str(), &data);

    if (parseResult != cgltf_result_success) {
        result.ErrorMsg = "cgltf_parse_file failed: " + std::to_string(parseResult);
        AYAYA_CORE_ERROR("glTF import error: {}", result.ErrorMsg);
        return result;
    }

    // Load buffers (reads .bin or embedded data)
    parseResult = cgltf_load_buffers(&options, data, filePath.c_str());
    if (parseResult != cgltf_result_success) {
        result.ErrorMsg = "cgltf_load_buffers failed: " + std::to_string(parseResult);
        AYAYA_CORE_ERROR("glTF import error: {}", result.ErrorMsg);
        cgltf_free(data);
        return result;
    }

    AYAYA_CORE_INFO("glTF: {} nodes, {} meshes, {} materials, {} textures, {} lights",
        data->nodes_count, data->meshes_count, data->materials_count,
        data->textures_count, data->lights_count);

    // === Convert Materials ===
    std::string modelDir = filePath.substr(0, filePath.find_last_of("/\\") + 1);
    std::vector<std::shared_ptr<Material>> materials(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        materials[i] = ConvertMaterial(&data->materials[i], modelDir, m_TextureCache);
    }
    result.MaterialCount = static_cast<int>(data->materials_count);

    // === Traverse Scene Graph ===
    cgltf_scene* gltfScene = data->scene ? data->scene : &data->scenes[0];
    Entity rootEntity; // scene root

    for (cgltf_size i = 0; i < gltfScene->nodes_count; ++i) {
        ParseNode(gltfScene->nodes[i], rootEntity, scene, data, materials);
    }
    result.NodeCount = static_cast<int>(data->nodes_count);
    result.MeshCount = static_cast<int>(data->meshes_count);
    result.TextureCount = static_cast<int>(data->textures_count);
    result.LightCount   = static_cast<int>(data->lights_count);

    result.Success = true;

    cgltf_free(data);
    return result;
}

} // namespace Ayaya
