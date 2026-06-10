#include "ayapch.h"
#include "AssetManager.hpp"
#include "Prefab.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/TextureCube.hpp"
#include "Renderer/Model.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/MaterialSerializer.hpp"
#include "Scene/SceneSerializer.hpp"
#include "Scene/Components.hpp"
#include "Project/Project.hpp"
#include "Core/VFS.hpp"
#include "Core/Log.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <thread> // 异步加载

namespace Ayaya {

    // 静态成员初始化
    std::vector<AssetManager::DeferredRelease> AssetManager::s_DeferredReleases;
    std::unordered_map<UUID, std::shared_ptr<void>> AssetManager::s_Assets;
    std::unordered_map<UUID, AssetMetadata> AssetManager::s_Registry;

    UUID AssetManager::s_BuiltInCubeHandle = 0;
    UUID AssetManager::s_BuiltInSphereHandle = 0;
    UUID AssetManager::s_BuiltInPlaneHandle = 0;
    UUID AssetManager::s_BuiltInMaterialHandle = 0;

    // 异步加载：防重入集合 + 主线程任务队列
    std::unordered_set<UUID> AssetManager::s_LoadingAssets;
    std::mutex AssetManager::s_LoadingMutex;
    std::queue<std::function<void()>> AssetManager::s_MainThreadQueue;
    std::mutex AssetManager::s_MainThreadQueueMutex;
    std::unordered_map<UUID, std::unordered_set<UUID>> AssetManager::s_ReverseDeps;

    void AssetManager::Init() {
        AYAYA_CORE_INFO("AssetManager Initialized.");
    }

    void AssetManager::Shutdown() {
        Clear();
        // Clean up .Trash folder on exit
        auto trashDir = Project::GetProjectDirectory() / ".Trash";
        if (std::filesystem::exists(trashDir)) {
            std::error_code ec;
            std::filesystem::remove_all(trashDir, ec);
            if (!ec) AYAYA_CORE_INFO("AssetManager: Cleaned up .Trash/");
        }
        AYAYA_CORE_INFO("AssetManager Shutdown.");
    }

    void AssetManager::Clear() {
        s_Assets.clear();
        s_Registry.clear();
    }

    // =====================================================================
    // .meta 文件系统 — 核心原语
    // =====================================================================

    // 辅助：序列化导入设置
    static void SerializeTextureSettings(YAML::Emitter& out, const TextureImportSettings& settings) {
        out << YAML::Key << "import_settings" << YAML::BeginMap;
        out << YAML::Key << "filter" << YAML::Value << (int)settings.Filter;
        out << YAML::Key << "wrap" << YAML::Value << (int)settings.Wrap;
        out << YAML::Key << "srgb" << YAML::Value << settings.SRGB;
        out << YAML::Key << "mipmaps" << YAML::Value << settings.GenerateMipmaps;
        out << YAML::Key << "flip_y" << YAML::Value << settings.FlipY;
        out << YAML::EndMap;
    }

    static void SerializeModelSettings(YAML::Emitter& out, const ModelImportSettings& settings) {
        out << YAML::Key << "model_settings" << YAML::BeginMap;
        out << YAML::Key << "global_scale" << YAML::Value << settings.GlobalScale;
        out << YAML::Key << "normals" << YAML::Value << (int)settings.Normals;
        out << YAML::Key << "tangents" << YAML::Value << (int)settings.Tangents;
        out << YAML::Key << "import_materials" << YAML::Value << settings.ImportMaterials;
        out << YAML::Key << "optimize_mesh" << YAML::Value << settings.OptimizeMesh;
        out << YAML::Key << "weld_vertices" << YAML::Value << settings.WeldVertices;
        out << YAML::Key << "mesh_compression" << YAML::Value << settings.MeshCompression;
        out << YAML::Key << "swap_yz" << YAML::Value << settings.SwapYZ;
        out << YAML::Key << "material_search_rule" << YAML::Value << (int)settings.MatSearchRule;
        out << YAML::Key << "import_animations" << YAML::Value << settings.ImportAnimations;
        out << YAML::Key << "merge_meshes" << YAML::Value << settings.MergeMeshes;
        out << YAML::Key << "combine_into_prefab" << YAML::Value << settings.CombineIntoPrefab;
        out << YAML::EndMap;
    }

    static ModelImportSettings DeserializeModelSettings(YAML::Node node) {
        ModelImportSettings settings;
        if (auto s = node["model_settings"]) {
            settings.GlobalScale      = s["global_scale"].as<float>(1.0f);
            settings.Normals          = (NormalMode)s["normals"].as<int>(0);
            settings.Tangents         = (TangentMode)s["tangents"].as<int>(0);
            settings.ImportMaterials  = s["import_materials"].as<bool>(true);
            settings.OptimizeMesh     = s["optimize_mesh"].as<bool>(true);
            settings.WeldVertices     = s["weld_vertices"].as<bool>(false);
            settings.MeshCompression  = s["mesh_compression"].as<bool>(false);
            settings.SwapYZ           = s["swap_yz"].as<bool>(false);
            settings.MatSearchRule    = (MaterialSearchRule)s["material_search_rule"].as<int>(0);
            settings.ImportAnimations = s["import_animations"].as<bool>(false);
            settings.MergeMeshes       = s["merge_meshes"].as<bool>(false);
            settings.CombineIntoPrefab = s["combine_into_prefab"].as<bool>(true);
        }
        return settings;
    }

    static TextureImportSettings DeserializeTextureSettings(YAML::Node node) {
        TextureImportSettings settings;
        if (auto s = node["import_settings"]) {
            settings.Filter          = (TextureFilterMode)s["filter"].as<int>(0);
            settings.Wrap            = (TextureWrapMode)s["wrap"].as<int>(0);
            settings.SRGB            = s["srgb"].as<bool>(true);
            settings.GenerateMipmaps = s["mipmaps"].as<bool>(true);
            settings.FlipY           = s["flip_y"].as<bool>(true);
        }
        return settings;
    }

    bool AssetManager::WriteMetaFile(const std::filesystem::path& assetPhysicalPath, UUID handle, AssetType type) {
        TextureImportSettings settings;
        if (s_Registry.count(handle)) settings = s_Registry[handle].TextureSettings;
        return WriteMetaFile(assetPhysicalPath, handle, type, settings);
    }

    bool AssetManager::WriteMetaFile(const std::filesystem::path& assetPhysicalPath, UUID handle, AssetType type, const TextureImportSettings& settings) {
        std::filesystem::path metaPath = assetPhysicalPath.string() + ".meta";
        try {
            std::string virtualPath = VFS::GetVirtualPath(assetPhysicalPath);

            YAML::Emitter out;
            out << YAML::BeginMap;
            out << YAML::Key << "uuid" << YAML::Value << static_cast<uint64_t>(handle);
            out << YAML::Key << "type" << YAML::Value << static_cast<int>(type);
            out << YAML::Key << "virtual_path" << YAML::Value << virtualPath;

            // Type-specific import settings
            if (type == AssetType::Texture2D)
                SerializeTextureSettings(out, settings);
            else if (type == AssetType::Model) {
                SerializeModelSettings(out, s_Registry.count(handle)
                    ? s_Registry[handle].ModelSettings : ModelImportSettings{});
            }

            out << YAML::EndMap;

            std::ofstream fout(metaPath);
            if (!fout.is_open()) {
                AYAYA_CORE_ERROR("AssetManager: Failed to write .meta file: {0}", metaPath.string());
                return false;
            }
            fout << out.c_str();
            fout.close();
            return true;
        } catch (const std::exception& e) {
            AYAYA_CORE_ERROR("AssetManager: Exception writing .meta file {0}: {1}", metaPath.string(), e.what());
            return false;
        }
    }

    bool AssetManager::ReadMetaFile(const std::filesystem::path& metaPath, UUID& outHandle, AssetType& outType) {
        return ReadMetaFile(metaPath, outHandle, outType, nullptr, nullptr);
    }

    bool AssetManager::ReadMetaFile(const std::filesystem::path& metaPath, UUID& outHandle, AssetType& outType, TextureImportSettings* outSettings, ModelImportSettings* outModelSettings) {
        try {
            YAML::Node data = YAML::LoadFile(metaPath.string());
            if (!data["uuid"] || !data["type"]) {
                AYAYA_CORE_WARN("AssetManager: Corrupted .meta file (missing uuid/type): {0}", metaPath.string());
                return false;
            }
            outHandle = UUID(data["uuid"].as<uint64_t>());
            outType = static_cast<AssetType>(data["type"].as<int>());
            if (outType == AssetType::None) {
                AYAYA_CORE_WARN("AssetManager: .meta file has AssetType::None, skipping: {0}", metaPath.string());
                return false;
            }
            if (outSettings) *outSettings = DeserializeTextureSettings(data);
            if (outModelSettings) *outModelSettings = DeserializeModelSettings(data);
            // Parse SubAssets for Model-type assets
            if (outType == AssetType::Model && data["sub_assets"]) {
                for (auto subNode : data["sub_assets"]) {
                    if (subNode["uuid"] && subNode["sub_mesh_index"].IsDefined()) {
                        UUID subHandle(subNode["uuid"].as<uint64_t>());
                        int subIdx = subNode["sub_mesh_index"].as<int>();
                        if (!s_Registry.count(subHandle)) {
                            AssetMetadata subMeta;
                            subMeta.Type = AssetType::SubMesh;
                            // Route through parent: VirtualPath = parent's, ParentHandle = parent UUID
                            subMeta.VirtualPath = data["virtual_path"]
                                ? data["virtual_path"].as<std::string>() : "";
                            subMeta.ParentHandle = outHandle;
                            subMeta.SubMeshIndex = subIdx;
                            s_Registry[subHandle] = subMeta;
                        }
                    }
                }
            }
            return true;
        } catch (const YAML::ParserException& e) {
            AYAYA_CORE_ERROR("AssetManager: Failed to parse .meta file {0}: {1}", metaPath.string(), e.what());
            return false;
        } catch (const std::exception& e) {
            AYAYA_CORE_ERROR("AssetManager: Error reading .meta file {0}: {1}", metaPath.string(), e.what());
            return false;
        }
    }

    // =====================================================================
    // RefreshRegistry — 递归扫描 .meta 文件重建内存注册表
    // =====================================================================
    // 判断文件扩展名是否支持导入
    static bool IsSupportedAssetFile(const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        return ext == ".png"  || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".bmp"  || ext == ".hdr" ||
               ext == ".obj"  || ext == ".fbx" || ext == ".gltf" || ext == ".glb" ||
               ext == ".mat"  || ext == ".lua" || ext == ".cube" ||
               ext == ".prefab" || ext == ".ayaya";
    }

    void AssetManager::RefreshRegistry() {
        s_Registry.clear();
        auto assetDir = Project::GetAssetDirectory();

        // === 自动发现：为没有 .meta 的资产文件自动生成 .meta ===
        if (std::filesystem::exists(assetDir)) {
            try {
                for (auto& entry : std::filesystem::recursive_directory_iterator(assetDir)) {
                    if (entry.is_directory()) continue;
                    if (entry.path().extension() == ".meta") continue;
                    if (!IsSupportedAssetFile(entry.path())) continue;
                    if (entry.path().string().find("/.Trash/") != std::string::npos) continue;

                    // 跳过已有 .meta 的文件
                    if (std::filesystem::exists(entry.path().string() + ".meta")) continue;

                    AYAYA_CORE_INFO("AssetManager: Auto-discovered asset without .meta, importing: {0}", entry.path().string());
                    ImportAsset(entry.path());
                }
            } catch (const std::exception& e) {
                AYAYA_CORE_ERROR("AssetManager: Auto-discovery scan failed: {0}", e.what());
            }
        }

        // === 扫描 .meta 文件重建注册表 ===
        if (std::filesystem::exists(assetDir)) {
            try {
                for (auto& entry : std::filesystem::recursive_directory_iterator(assetDir)) {
                    if (entry.path().string().find("/.Trash/") != std::string::npos) continue;
                    if (entry.path().extension() == ".meta") {
                        std::string metaPath = entry.path().string();
                        std::string sourcePath = metaPath.substr(0, metaPath.size() - 5);

                        // 拦截孤儿 .meta：源文件已被删除，同步删除 .meta
                        if (!std::filesystem::exists(sourcePath)) {
                            AYAYA_CORE_WARN("AssetManager: Deleting orphan .meta file (source missing): {0}", metaPath);
                            std::filesystem::remove(metaPath);
                            continue;
                        }

                        UUID handle;
                        AssetType type;
                        TextureImportSettings texSettings;
                        ModelImportSettings modelSettings;
                        if (!ReadMetaFile(entry.path(), handle, type, &texSettings, &modelSettings))
                            continue;

                        std::string vPath = VFS::GetVirtualPath(sourcePath);
                        s_Registry[handle] = { type, vPath, texSettings, modelSettings };

                        // Backfill .meta: rewrite if missing virtual_path (old-format .meta)
                        {
                            try {
                                YAML::Node data = YAML::LoadFile(metaPath);
                                if (!data["virtual_path"])
                                    RewriteMetaFile(handle);
                            } catch (...) {}
                        }

                        if (type == AssetType::Texture2D) {
                            RequestAsyncLoad(handle);
                        }
                    }
                }
            } catch (const std::exception& e) {
                AYAYA_CORE_ERROR("AssetManager: RefreshRegistry scan failed: {0}", e.what());
            }
        }

        LoadEngineAssets();

        AYAYA_CORE_INFO("AssetManager: RefreshRegistry complete — {0} assets registered", s_Registry.size());
    }

    // =====================================================================
    // LoadEngineAssets — 从 EngineAssets.yaml 加载引擎内部资产
    // =====================================================================
    void AssetManager::LoadEngineAssets() {
        std::string tablePath = VFS::ResolveString("engine://Editor/EngineAssets.yaml");
        if (!std::filesystem::exists(tablePath)) {
            AYAYA_CORE_WARN("AssetManager: EngineAssets.yaml not found at {0}", tablePath);
            return;
        }

        try {
            YAML::Node data = YAML::LoadFile(tablePath);
            auto assetsNode = data["EngineAssets"];
            if (!assetsNode || !assetsNode.IsSequence()) {
                AYAYA_CORE_ERROR("AssetManager: EngineAssets.yaml is corrupted");
                return;
            }

            int loadedCount = 0;
            for (auto assetNode : assetsNode) {
                UUID handle = assetNode["uuid"].as<uint64_t>();
                AssetType type = static_cast<AssetType>(assetNode["type"].as<int>());
                std::string virtualPath = assetNode["virtual_path"].as<std::string>();

                // 不覆盖项目 .meta 中已注册的同路径资产
                bool alreadyExists = false;
                for (const auto& [h, meta] : s_Registry) {
                    if (meta.VirtualPath == virtualPath) { alreadyExists = true; break; }
                }
                if (!alreadyExists) {
                    s_Registry[handle] = { type, virtualPath };
                    loadedCount++;
                }
            }

            AYAYA_CORE_INFO("AssetManager: Loaded {0} engine assets from EngineAssets.yaml ({1})",
                             loadedCount, tablePath);
        } catch (const std::exception& e) {
            AYAYA_CORE_ERROR("AssetManager: Failed to load EngineAssets.yaml: {0}", e.what());
        }
    }

    // =====================================================================
    // MigrateFromRegistry — 一次性从旧 AssetRegistry.yaml 迁移到 .meta 文件
    // =====================================================================
    bool AssetManager::MigrateFromRegistry(const std::string& registryPath) {
        if (!std::filesystem::exists(registryPath)) {
            AYAYA_CORE_INFO("AssetManager: No AssetRegistry.yaml found, skipping .meta migration.");
            return false;
        }

        YAML::Node data;
        try {
            data = YAML::LoadFile(registryPath);
        } catch (const YAML::ParserException& e) {
            AYAYA_CORE_ERROR("AssetManager: Failed to parse AssetRegistry.yaml during migration: {0}", e.what());
            return false;
        }

        auto registryNode = data["AssetRegistry"];
        if (!registryNode || !registryNode.IsSequence()) {
            AYAYA_CORE_ERROR("AssetManager: AssetRegistry.yaml is corrupted, cannot migrate.");
            return false;
        }

        int migrated = 0;
        int skipped = 0;

        for (auto assetNode : registryNode) {
            try {
                UUID handle = assetNode["Handle"].as<uint64_t>();
                AssetType type = static_cast<AssetType>(assetNode["Type"].as<int>());
                std::string virtualPath = assetNode["VirtualPath"].as<std::string>();

                // 跳过内置/引擎资产 — 它们没有物理文件，不需要 .meta
                if (virtualPath.rfind("Primitive::", 0) == 0 ||
                    virtualPath.rfind("engine://", 0) == 0) {
                    skipped++;
                    continue;
                }

                std::string physicalPath = VFS::ResolveString(virtualPath);
                if (!std::filesystem::exists(physicalPath)) {
                    skipped++;
                    continue;
                }

                if (WriteMetaFile(physicalPath, handle, type))
                    migrated++;
            } catch (...) {
                skipped++;
            }
        }

        AYAYA_CORE_INFO("AssetManager: Migration complete — {0} .meta files created, {1} skipped", migrated, skipped);
        return migrated > 0;
    }

    // =====================================================================
    // UnloadUnusedAssets — Mark & Sweep 垃圾回收的 Sweep 阶段
    // =====================================================================
    void AssetManager::UnloadUnusedAssets(const std::unordered_set<UUID>& activeHandles) {
        // 保护所有引擎资产（与 EngineAssets.yaml 保持一致）
        static const std::unordered_set<uint64_t> kBuiltInHandles = {
            16140901000000000001ull, // Cube
            16140901000000000002ull, // Sphere
            16140901000000000003ull, // Plane
            16140901000000000004ull, // DefaultPBR.mat
            16140901000000000005ull, // HDR skybox
            16140901000000000006ull, // Sky cubemap
        };

        std::vector<UUID> toErase;
        for (const auto& [handle, asset] : s_Assets) {
            if (kBuiltInHandles.count(handle)) continue;
            if (activeHandles.find(handle) == activeHandles.end()) {
                toErase.push_back(handle);
            }
        }

        for (UUID handle : toErase) {
            // Deferred release: keep GPU resources alive for 3 frames
            auto it = s_Assets.find(handle);
            if (it != s_Assets.end()) {
                DeferredRelease dr;
                dr.Asset = it->second;
                dr.FramesRemaining = 3;
                s_DeferredReleases.push_back(std::move(dr));
                s_Assets.erase(it);
            }
            AYAYA_CORE_INFO("GC: Unloaded unused asset {0}", (uint64_t)handle);
        }

        if (!toErase.empty())
            AYAYA_CORE_INFO("GC: Sweep complete — {0} assets freed", toErase.size());
    }

    void AssetManager::RewriteMetaFile(UUID handle) {
        auto& meta = s_Registry[handle];
        std::string physicalPath = VFS::ResolveString(meta.VirtualPath);
        if (physicalPath.empty()) return;
        std::filesystem::path metaPath = physicalPath + ".meta";
        try {
            YAML::Emitter out;
            out << YAML::BeginMap;
            out << YAML::Key << "uuid" << YAML::Value << static_cast<uint64_t>(handle);
            out << YAML::Key << "type" << YAML::Value << static_cast<int>(meta.Type);
            out << YAML::Key << "virtual_path" << YAML::Value << meta.VirtualPath;

            if (meta.Type == AssetType::Texture2D)
                SerializeTextureSettings(out, meta.TextureSettings);
            else if (meta.Type == AssetType::Model)
                SerializeModelSettings(out, meta.ModelSettings);

            // Re-serialize sub_assets for Model types — preserve SubMesh UUIDs
            if (meta.Type == AssetType::Model) {
                out << YAML::Key << "sub_assets" << YAML::Value << YAML::BeginSeq;
                for (const auto& [subHandle, subMeta] : s_Registry) {
                    if (subMeta.Type == AssetType::SubMesh && subMeta.ParentHandle == handle) {
                        out << YAML::BeginMap;
                        out << YAML::Key << "uuid" << YAML::Value << static_cast<uint64_t>(subHandle);
                        out << YAML::Key << "sub_mesh_index" << YAML::Value << subMeta.SubMeshIndex;
                        out << YAML::Key << "type" << YAML::Value << static_cast<int>(AssetType::SubMesh);
                        out << YAML::EndMap;
                    }
                }
                out << YAML::EndSeq;
            }

            out << YAML::EndMap;

            std::ofstream fout(metaPath);
            if (!fout.is_open()) {
                AYAYA_CORE_ERROR("AssetManager: Failed to rewrite .meta: {0}", metaPath.string());
                return;
            }
            fout << out.c_str();
            fout.close();
        } catch (const std::exception& e) {
            AYAYA_CORE_ERROR("AssetManager: Exception rewriting .meta {0}: {1}", metaPath.string(), e.what());
        }
    }

    // =====================================================================
    // UpdateMetadataSettings — 更新纹理/模型导入设置并覆写 .meta 文件
    // =====================================================================
    void AssetManager::UpdateMetadataSettings(UUID handle, const TextureImportSettings& settings) {
        if (!s_Registry.count(handle)) return;
        s_Registry[handle].TextureSettings = settings;
        RewriteMetaFile(handle);
    }

    void AssetManager::UpdateMetadataSettings(UUID handle, const ModelImportSettings& settings) {
        if (!s_Registry.count(handle)) return;
        s_Registry[handle].ModelSettings = settings;
        RewriteMetaFile(handle);
    }

    // =====================================================================
    // ReloadAsset — 强制同步重载资产（Apply 按钮专用，确保设置即时生效）
    // =====================================================================
    void AssetManager::ReloadAsset(UUID handle) {
        // Defer destruction of old asset: keep it alive for 3 frames so
        // in-flight GPU command buffers can finish referencing its Vulkan resources.
        auto it = s_Assets.find(handle);
        if (it != s_Assets.end()) {
            DeferredRelease dr;
            dr.Asset = it->second;
            dr.FramesRemaining = 3;
            s_DeferredReleases.push_back(std::move(dr));
            s_Assets.erase(it);
        }

        {
            std::lock_guard<std::mutex> lock(s_LoadingMutex);
            s_LoadingAssets.erase(handle);
        }

        if (s_Registry.count(handle)) {
            LoadAssetFromFile(handle);
        }
    }

    // =====================================================================
    // ImportModelAssetSync — background-thread safe model import
    // CPU-heavy: Assimp ReadFile + mesh processing + material generation.
    // Does NOT touch s_Registry or any GPU state.
    // =====================================================================
    ImportResult AssetManager::ImportModelAssetSync(const std::filesystem::path& filepath,
                                                      const ModelImportSettings& settings) {
        ImportResult result;
        result.ModelHandle = UUID();
        result.Success = false;

        std::string pathStr = filepath.string();

        // 1. Determine the project asset directory
        std::string assetDir;
        if (Project::GetActive()) {
            assetDir = Project::GetAssetDirectory().string();
        } else {
            assetDir = filepath.parent_path().string();
        }
        std::replace(assetDir.begin(), assetDir.end(), '\\', '/');

        std::string baseName = filepath.stem().string();
        std::string ext = filepath.extension().string();
        // Normalize extension to lowercase
        for (auto& c : ext) c = (char)std::tolower(c);

        // 2. Copy the source model file into the project asset directory.
        //    Deduplicate: if a file with the same name exists, append a suffix.
        std::string destFilename = baseName + ext;
        std::string destPath = assetDir + "/" + destFilename;
        int suffix = 1;
        while (std::filesystem::exists(destPath)) {
            destFilename = baseName + "_" + std::to_string(suffix) + ext;
            destPath = assetDir + "/" + destFilename;
            suffix++;
        }
        try {
            std::filesystem::copy_file(pathStr, destPath);
        } catch (const std::exception& e) {
            result.ErrorMsg = std::string("Failed to copy model file: ") + e.what();
            return result;
        }

        // 3. Write .meta IMMEDIATELY so AssetWatcher/ImportAsset sees the same UUID
        result.ModelVirtualPath = VFS::GetVirtualPath(destPath);
        WriteMetaFile(destPath, result.ModelHandle, AssetType::Model);

        // 4. Load the model from the project copy with settings
        auto model = std::make_shared<Model>(destPath, settings);
        if (model->GetMeshes().empty()) {
            result.ErrorMsg = "Assimp failed to load model or model has no meshes";
            // Clean up the copied file on failure
            std::filesystem::remove(destPath);
            return result;
        }

        result.Success = true;

        // Recompute baseName from the actual destination file (may have suffix)
        baseName = std::filesystem::path(destPath).stem().string();

        // 5. Generate material entries — one per UNIQUE Assimp material index
        if (settings.ImportMaterials) {
            std::set<int> uniqueMatIndices;
            for (auto& mesh : model->GetMeshes())
                uniqueMatIndices.insert(mesh->GetMaterialIndex());

            for (int matIdx : uniqueMatIndices) {
                ImportResult::MatEntry matEntry;
                matEntry.Handle = UUID();
                matEntry.AssimpMaterialIndex = matIdx;
                std::string matName = (uniqueMatIndices.size() == 1)
                    ? baseName + "_Material"
                    : baseName + "_Material" + std::to_string(matIdx);
                matEntry.PhysicalPath = assetDir + "/" + matName + ".mat";
                matEntry.VirtualPath = "project://" + matName + ".mat";
                result.Materials.push_back(matEntry);
            }
        }

        // 6. Copy selected texture files into the project asset directory
        for (auto& texPath : settings.TextureFiles) {
            if (!std::filesystem::exists(texPath)) continue;
            std::string texFilename = texPath.filename().string();
            std::string texDest = assetDir + "/" + texFilename;
            // Deduplicate
            int texSuffix = 1;
            while (std::filesystem::exists(texDest)) {
                std::string stem = texPath.stem().string();
                std::string ext = texPath.extension().string();
                texDest = assetDir + "/" + stem + "_" + std::to_string(texSuffix) + ext;
                texSuffix++;
            }
            try {
                std::filesystem::copy_file(texPath, texDest);
                ImportResult::TexEntry texEntry;
                texEntry.PhysicalPath = texDest;
                texEntry.Handle = UUID();
                result.CopiedTextures.push_back(texEntry);
                WriteMetaFile(texDest, texEntry.Handle, AssetType::Texture2D, TextureImportSettings{});
            } catch (const std::exception& e) {
                AYAYA_CORE_WARN("AssetManager: Failed to copy texture '{0}': {1}",
                    texPath.string(), e.what());
            }
        }

        // 6. Generate SubAsset entries — one per mesh, each with its own UUID.
        //    Stores the UUID→mesh mapping for the prefab builder to use per-entity.
        //    MergeMeshes=true skips this (only one merged mesh → uses result.ModelHandle).
        if (!settings.MergeMeshes) {
            int meshIdx = 0;
            for (auto& mesh : model->GetMeshes()) {
                ImportResult::MeshEntry entry;
                entry.Handle = UUID();
                entry.SubMeshIndex = meshIdx;
                entry.Name = baseName + "_Mesh" + std::to_string(meshIdx);
                entry.VirtualPath = "project://" + entry.Name;
                entry.PhysicalPath = ""; // virtual sub-asset, no separate file
                result.SubMeshes.push_back(entry);
                meshIdx++;
            }
        }

        // 7. Build Prefab if requested — creates entity hierarchy in a Scene, saves to .prefab
        if (settings.CombineIntoPrefab) {
            result.PrefabHandle = UUID();
            result.PrefabPath = assetDir + "/" + baseName + ".prefab";

            auto prefab = std::make_shared<Prefab>();
            Scene* prefabScene = prefab->GetScene();

            // Recursive function to build entity hierarchy from ModelNode tree.
            // meshIdx tracks the global mesh index across all nodes for SubMesh UUID lookup.
            int globalMeshIdx = 0;

            std::function<Entity(const ModelNode&, Entity)> buildNode =
                [&](const ModelNode& node, Entity parent) -> Entity {
                Entity entity = prefabScene->CreateEntity(
                    node.Name.empty() ? "ModelNode" : node.Name);

                // Transform
                auto& transform = entity.GetComponent<TransformComponent>();
                glm::vec3 scale;
                glm::quat rotation;
                glm::vec3 translation;
                glm::vec3 skew;
                glm::vec4 perspective;
                glm::decompose(node.LocalTransform, scale, rotation, translation, skew, perspective);
                transform.Translation = translation;
                transform.Rotation = glm::eulerAngles(rotation);
                transform.Scale = scale;

                // Hierarchy — use SetParent to properly manage m_RootEntities
                if (parent) {
                    entity.SetParent(parent, false);
                }

                // Mesh renderer — reference the correct SubAsset UUID (or parent for merged).
                // Mirror InstantiateModelNode behavior: single mesh → attach to node entity;
                // multiple meshes → create sub-entities (one per mesh).
                if (!node.Meshes.empty()) {
                    // Helper: assign ModelHandle + MaterialHandle for one mesh
                    auto assignMeshRenderer = [&](MeshRendererComponent& mrc, int localMeshIdx) {
                        mrc.ModelHandle = (globalMeshIdx < (int)result.SubMeshes.size())
                            ? result.SubMeshes[globalMeshIdx].Handle
                            : result.ModelHandle;
                        int meshMatIdx = node.Meshes[localMeshIdx]->GetMaterialIndex();
                        UUID matHandle = UUID(16140901000000000004ull); // built-in DefaultPBR
                        for (auto& mat : result.Materials) {
                            if (mat.AssimpMaterialIndex == meshMatIdx) {
                                matHandle = mat.Handle; break;
                            }
                        }
                        mrc.MaterialHandle = matHandle;
                        globalMeshIdx++;
                    };

                    if (node.Meshes.size() == 1) {
                        // Single mesh: attach renderer directly to the node entity
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        assignMeshRenderer(mrc, 0);
                    } else {
                        // Multiple meshes: create one sub-entity per mesh, matching
                        // InstantiateModelNode behavior (Scene.cpp lines 153-171).
                        for (size_t i = 0; i < node.Meshes.size(); i++) {
                            std::string subName = node.Name.empty()
                                ? "SubMesh_" + std::to_string(i)
                                : node.Name + "_SubMesh_" + std::to_string(i);
                            Entity subEntity = prefabScene->CreateEntity(subName);
                            subEntity.SetParent(entity, false);

                            auto& mrc = subEntity.AddComponent<MeshRendererComponent>();
                            assignMeshRenderer(mrc, (int)i);
                        }
                    }
                }

                // Recurse into children
                for (const auto& childNode : node.Children) {
                    buildNode(childNode, entity);
                }

                return entity;
            };

            Entity rootEntity = buildNode(model->GetRootNode(), Entity{});
            // Override root entity name to the imported file's name
            rootEntity.GetComponent<TagComponent>().Tag = baseName;
            prefab->SetRootEntity(rootEntity);

            // Serialize the prefab to disk
            prefab->Save(result.PrefabPath);

            AYAYA_CORE_INFO("AssetManager: Prefab saved to {0}", result.PrefabPath);
        }

        return result;
    }

    // =====================================================================
    // FinalizeModelImport — main-thread only, called via SubmitToMainThread
    // Writes .meta files, registers sub-assets in s_Registry, notifies watcher.
    // =====================================================================
    void AssetManager::FinalizeModelImport(const ImportResult& result) {
        if (!result.Success) {
            AYAYA_CORE_ERROR("AssetManager: Model import failed: {0}", result.ErrorMsg);
            return;
        }

        // 1. Register the main model asset
        AssetType modelType = AssetType::Model;
        s_Registry[result.ModelHandle] = { modelType, result.ModelVirtualPath,
                                           TextureImportSettings{}, ModelImportSettings{} };
        std::string modelPhysPath = VFS::ResolveString(result.ModelVirtualPath);
        if (!modelPhysPath.empty() && !std::filesystem::exists(modelPhysPath + ".meta")) {
            WriteMetaFile(modelPhysPath, result.ModelHandle, modelType);
        }

        // 1b. Register SubAssets (per-mesh standalone Models) and write .meta
        if (!result.SubMeshes.empty()) {
            auto parentModel = AssetManager::GetAsset<Model>(result.ModelHandle);
            if (parentModel) {
                int idx = 0;
                for (auto& sub : result.SubMeshes) {
                    auto meshes = parentModel->GetMeshes();
                    if (idx < (int)meshes.size()) {
                        auto subModel = std::make_shared<Model>(meshes[idx]);
                        subModel->GetRootNode().LocalTransform = parentModel->GetRootNode().LocalTransform;
                        AssetManager::AddAsset<Model>(sub.Handle, subModel);
                        AssetMetadata subMeta;
                        subMeta.Type = AssetType::SubMesh;
                        subMeta.VirtualPath = result.ModelVirtualPath;  // parent's path for routing
                        subMeta.ParentHandle = result.ModelHandle;
                        subMeta.SubMeshIndex = idx;
                        s_Registry[sub.Handle] = subMeta;
                    }
                    idx++;
                }
            }
            // Re-write .meta with SubAsset UUIDs included
            if (!modelPhysPath.empty()) {
                std::filesystem::path metaPath = modelPhysPath + ".meta";
                try {
                    YAML::Emitter out;
                    out << YAML::BeginMap;
                    out << YAML::Key << "uuid" << YAML::Value << static_cast<uint64_t>(result.ModelHandle);
                    out << YAML::Key << "type" << YAML::Value << static_cast<int>(AssetType::Model);
                    SerializeModelSettings(out, s_Registry[result.ModelHandle].ModelSettings);
                    out << YAML::Key << "virtual_path" << YAML::Value << result.ModelVirtualPath;
                    out << YAML::Key << "sub_assets" << YAML::Value << YAML::BeginSeq;
                    for (auto& sub : result.SubMeshes) {
                        out << YAML::BeginMap;
                        out << YAML::Key << "uuid" << YAML::Value << static_cast<uint64_t>(sub.Handle);
                        out << YAML::Key << "name" << YAML::Value << sub.Name;
                        out << YAML::Key << "sub_mesh_index" << YAML::Value << sub.SubMeshIndex;
                        out << YAML::Key << "type" << YAML::Value << static_cast<int>(AssetType::SubMesh);
                        out << YAML::EndMap;
                    }
                    out << YAML::EndSeq;
                    out << YAML::EndMap;
                    std::ofstream fout(metaPath);
                    if (fout.is_open()) fout << out.c_str();
                } catch (...) {}
            }
        }

        // 2. Import copied textures via ImportAsset (registers UUID in s_Registry,
        //    creates GPU texture). The function reads the pre-generated UUID from .meta.
        struct TexMatch { UUID Handle; std::string Suffix; };
        std::vector<TexMatch> texMatches;
        for (auto& tex : result.CopiedTextures) {
            UUID texHandle = ImportAsset(tex.PhysicalPath);
            if (texHandle != 0) {
                std::string stem = std::filesystem::path(tex.PhysicalPath).stem().string();
                texMatches.push_back({texHandle, stem});
            }
        }

        // ---- Advanced suffix-based fuzzy matching ----
        // Priority-ordered suffix lists for each material slot.
        // Earlier entries are checked first; first match wins.
        // Match suffix at end-of-string. Suffixes like "_basecolor" have a built-in
        // "_" boundary — no extra check needed. Raw suffixes like "basecolor" require
        // a preceding "_" or "." to avoid matching inside another word.
        auto WordEndsWith = [](const std::string& str, const std::string& sfx) -> bool {
            if (str.length() >= sfx.length()) {
                if (str.compare(str.length() - sfx.length(), sfx.length(), sfx) == 0) {
                    if (!sfx.empty() && sfx[0] == '_') return true;
                    size_t pos = str.length() - sfx.length();
                    if (pos > 0 && str[pos-1] != '_' && str[pos-1] != '.') return false;
                    return true;
                }
            }
            // Bare filename match: "diffuse" matches "_diffuse" (sfx minus leading "_")
            if (!sfx.empty() && sfx[0] == '_' && str == sfx.substr(1))
                return true;
            return false;
        };

        auto MatchBySuffix = [&](const std::vector<const char*>& suffixes) -> TexMatch* {
            for (auto* suffix : suffixes) {
                std::string sfx(suffix);
                for (auto& c : sfx) c = (char)std::tolower(c);
                for (auto& tm : texMatches) {
                    std::string lower = tm.Suffix;
                    for (auto& c : lower) c = (char)std::tolower(c);
                    if (WordEndsWith(lower, sfx))
                        return &tm;
                }
            }
            return nullptr;
        };

        // 3. Register materials — clone DefaultPBR, assign textures, serialize .mat
        for (auto& mat : result.Materials) {
            if (!std::filesystem::exists(mat.PhysicalPath + ".meta")) {
                auto baseMat = GetAsset<Material>(GetBuiltInMaterial());
                auto material = baseMat ? baseMat->Clone() : std::make_shared<Material>();
                material->Name = std::filesystem::path(mat.PhysicalPath).stem().string();
                material->AssetPath = mat.PhysicalPath;
                material->ShaderName = "PBR";

                // Helper: assign texture + enable flag
                auto AssignTex = [&](const std::string& mapProp, const std::string& useProp,
                                     TexMatch* tm) {
                    if (!tm) return;
                    material->SetTexture(mapProp, tm->Handle);
                    material->SetBool(useProp, true);
                };

                // --- 1. Albedo / BaseColor ---
                TexMatch* albedo = MatchBySuffix({
                    "_basecolor", "_albedo", "_bc", "_color", "_d", "_diffuse"
                });
                AssignTex("u_AlbedoMap", "u_UseAlbedoMap", albedo);

                // --- 2. Normal ---
                TexMatch* normal = MatchBySuffix({
                    "_normal", "_nrm", "_nor", "_n"
                });
                AssignTex("u_NormalMap", "u_UseNormalMap", normal);

                // --- 3. ORM (packed Occlusion-Roughness-Metallic) — check FIRST ---
                TexMatch* orm = MatchBySuffix({
                    "_occlusionroughnessmetallic", "_orm", "_arm"
                });
                if (orm) {
                    // Set ORM as a combined texture property
                    material->SetTexture("u_ORMMap", orm->Handle);
                    material->SetBool("u_UseORMMap", true);
                    // Disable individual maps to prevent conflicts
                    material->SetBool("u_UseMetallicMap", false);
                    material->SetBool("u_UseRoughnessMap", false);
                    material->SetBool("u_UseAOMap", false);
                } else {
                    // --- 4a. Metallic (individual) ---
                    TexMatch* metallic = MatchBySuffix({
                        "_metallic", "_metalness", "_metal", "_specular", "_spec", "_mt", "_m"
                    });
                    AssignTex("u_MetallicMap", "u_UseMetallicMap", metallic);

                    // --- 4b. Roughness (individual) ---
                    TexMatch* roughness = MatchBySuffix({
                        "_roughness", "_rough", "_r"
                    });
                    AssignTex("u_RoughnessMap", "u_UseRoughnessMap", roughness);

                    // --- 4c. AO (individual) ---
                    TexMatch* ao = MatchBySuffix({
                        "_ambientocclusion", "_ao", "_ambient", "_o"
                    });
                    AssignTex("u_AOMap", "u_UseAOMap", ao);
                }

                // --- 5. Height / Displacement ---
                TexMatch* height = MatchBySuffix({
                    "_height", "_displacement", "_disp", "_h"
                });
                AssignTex("u_HeightMap", "u_UseHeightMap", height);

                // --- 6. Emissive ---
                TexMatch* emissive = MatchBySuffix({
                    "_emissive", "_emission", "_emit", "_e"
                });
                AssignTex("u_EmissiveMap", "u_UseEmissiveMap", emissive);

                // --- 7. Opacity / Alpha ---
                TexMatch* opacity = MatchBySuffix({
                    "_opacity", "_alpha", "_op"
                });
                AssignTex("u_AlphaMap", "u_UseAlphaMap", opacity);

                // Write material to disk and register
                MaterialSerializer::Serialize(material, mat.PhysicalPath);
                s_Registry[mat.Handle] = { AssetType::Material, mat.VirtualPath,
                                           TextureImportSettings{}, ModelImportSettings{} };
                WriteMetaFile(mat.PhysicalPath, mat.Handle, AssetType::Material);
                s_Assets[mat.Handle] = material;
            }
        }

        // 3.5 Apply sRGB=false for linear data textures (ImportAsset auto-detects, but
        //     ORM textures may be missed — ensure they're linear).
        for (auto& tm : texMatches) {
            if (!s_Registry.count(tm.Handle)) continue;
            std::string lower = tm.Suffix;
            for (auto& c : lower) c = (char)std::tolower(c);
            bool isLinear = (lower.find("normal")   != std::string::npos ||
                             lower.find("_nrm")      != std::string::npos ||
                             lower.find("_nor")      != std::string::npos ||
                             lower.find("metallic")  != std::string::npos ||
                             lower.find("metalness") != std::string::npos ||
                             lower.find("specular")  != std::string::npos ||
                             lower.find("_spec")     != std::string::npos ||
                             lower.find("roughness") != std::string::npos ||
                             lower.find("_orm")      != std::string::npos ||
                             lower.find("_arm")      != std::string::npos ||
                             lower.find("occlusion") != std::string::npos ||
                             lower.find("_ao")       != std::string::npos ||
                             lower.find("ambient")   != std::string::npos ||
                             lower.find("height")    != std::string::npos ||
                             lower.find("displace")  != std::string::npos);
            if (isLinear) {
                s_Registry[tm.Handle].TextureSettings.SRGB = false;
                RewriteMetaFile(tm.Handle);
            }
        }

        // 4. If prefab was generated, register and load it into in-memory cache
        if (result.PrefabHandle != 0 && !result.PrefabPath.empty()) {
            if (!std::filesystem::exists(result.PrefabPath + ".meta")) {
                std::string prefabVPath = "project://" +
                    std::filesystem::path(result.PrefabPath).filename().string();
                s_Registry[result.PrefabHandle] = { AssetType::Prefab, prefabVPath,
                                                     TextureImportSettings{}, ModelImportSettings{} };
                WriteMetaFile(result.PrefabPath, result.PrefabHandle, AssetType::Prefab);
            }
            // Load the serialized prefab into in-memory cache
            auto loadedPrefab = std::make_shared<Prefab>();
            if (loadedPrefab->Load(result.PrefabPath)) {
                s_Assets[result.PrefabHandle] = loadedPrefab;
            }
        }

        AYAYA_CORE_INFO("AssetManager: Model import finalized — {0} materials, prefab={1}",
            result.Materials.size(),
            result.PrefabHandle != 0 ? "yes" : "no");
    }

    // =====================================================================
    // 主线程任务队列 — 后台线程完成 CPU 加载后，提交 GPU 上传回调在此执行
    // =====================================================================

    void AssetManager::SubmitToMainThread(const std::function<void()>& function) {
        std::lock_guard<std::mutex> lock(s_MainThreadQueueMutex);
        s_MainThreadQueue.push(function);
    }

    void AssetManager::Update() {
        // 快速交换出队列，避免长时间持锁阻塞后台线程
        std::queue<std::function<void()>> executeQueue;
        {
            std::lock_guard<std::mutex> lock(s_MainThreadQueueMutex);
            std::swap(s_MainThreadQueue, executeQueue);
        }

        // 在主线程安全执行所有 GPU 绑定任务
        while (!executeQueue.empty()) {
            executeQueue.front()();
            executeQueue.pop();
        }

        // Drain deferred releases: old GPU resources are kept alive for 3 frames
        // so in-flight command buffers can finish using them before destruction.
        for (auto it = s_DeferredReleases.begin(); it != s_DeferredReleases.end(); ) {
            it->FramesRemaining--;
            if (it->FramesRemaining <= 0) {
                it = s_DeferredReleases.erase(it);  // shared_ptr drops → destructor runs
            } else {
                ++it;
            }
        }
    }

    // =====================================================================
    // 资产导入逻辑：利用 VFS 智能识别项目还是引擎，并分配 UUID
    // =====================================================================
    UUID AssetManager::ImportAsset(const std::filesystem::path& filepath) {
        std::string virtualPath = VFS::GetVirtualPath(filepath);

        // 检查是否已经导入过（通过查找是否存在 .meta 文件）
        if (std::filesystem::exists(filepath.string() + ".meta")) {
            UUID existing = FindHandleForPath(filepath);
            if (existing != 0) return existing;
            // .meta exists but not yet in s_Registry (e.g. written by background thread).
            // Read UUID from .meta and register it now.
            UUID metaHandle; AssetType metaType;
            TextureImportSettings texSet;
            if (ReadMetaFile(filepath.string() + ".meta", metaHandle, metaType, &texSet)) {
                s_Registry[metaHandle] = { metaType, virtualPath, texSet, ModelImportSettings{} };
                return metaHandle;
            }
            // Corrupt .meta — fall through to re-import
        }

        // 查重：防止同一个文件在内存账本中被分配多个 UUID
        for (const auto& [handle, metadata] : s_Registry) {
            if (metadata.VirtualPath == virtualPath) return handle;
        }

        // 确定资产类型
        AssetType type = AssetType::None;
        std::string ext = filepath.extension().string();
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr" || ext == ".bmp") type = AssetType::Texture2D;
        else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") type = AssetType::Model;
        else if (ext == ".mat")                              type = AssetType::Material;
        else if (ext == ".prefab")                           type = AssetType::Prefab;
        else if (ext == ".lua")                              type = AssetType::LuaScript;
        else if (ext == ".cube")                             type = AssetType::TextureCube;
        else if (ext == ".ayaya")                            type = AssetType::Scene;

        if (type == AssetType::None) {
            AYAYA_CORE_WARN("AssetManager: Unsupported asset format '{0}'", ext);
            return 0;
        }

        UUID newHandle = UUID();

        // 对于贴图资产，根据文件名推断线性数据（法线/金属度等不启用 sRGB）
        TextureImportSettings texSettings;
        if (type == AssetType::Texture2D) {
            std::string lowerPath = filepath.string();
            for (auto& c : lowerPath) c = (char)std::tolower(c);
            bool isLinearData = (lowerPath.find("normal")   != std::string::npos ||
                                 lowerPath.find("metallic") != std::string::npos ||
                                 lowerPath.find("roughness")!= std::string::npos ||
                                 lowerPath.find("ao.")     != std::string::npos ||
                                 lowerPath.find("height")  != std::string::npos ||
                                 lowerPath.find("displace")!= std::string::npos);
            if (isLinearData) texSettings.SRGB = false;
        }

        // 写入独立的 .meta 文件（引擎资产和内置几何体不写 .meta）
        bool isEngineAsset = (virtualPath.rfind("engine://", 0) == 0) ||
                             (virtualPath.rfind("Primitive::", 0) == 0);
        if (!isEngineAsset) {
            WriteMetaFile(filepath, newHandle, type, texSettings);
        }

        // 注册到内存账本
        s_Registry[newHandle] = { type, virtualPath, texSettings };

        AYAYA_CORE_INFO("AssetManager: Imported asset '{0}' with UUID {1}", virtualPath, (uint64_t)newHandle);
        return newHandle;
    }

    // =====================================================================
    // 懒加载核心：根据账本信息，真正执行硬盘读取的地方（同步，主线程安全）
    // =====================================================================
    std::shared_ptr<void> AssetManager::LoadAssetFromFile(UUID handle) {
        const auto& metadata = s_Registry[handle];

        // 依赖 VFS 将虚拟路径解析为当前电脑的绝对物理路径
        std::string physicalPath = VFS::ResolveString(metadata.VirtualPath);
        std::shared_ptr<void> asset = nullptr;

        if (metadata.Type == AssetType::Texture2D) {
            asset = Texture2D::Create(physicalPath);
        }
        else if (metadata.Type == AssetType::SubMesh) {
            // SubMesh: route through parent Model
            if (metadata.ParentHandle != 0 && metadata.SubMeshIndex >= 0) {
                auto parent = AssetManager::GetAsset<Model>(metadata.ParentHandle);
                if (!parent) {
                    parent = std::static_pointer_cast<Model>(LoadAssetFromFile(metadata.ParentHandle));
                    if (parent) s_Assets[metadata.ParentHandle] = parent;
                }
                if (parent) {
                    auto meshes = parent->GetMeshes();
                    if (metadata.SubMeshIndex < (int)meshes.size()) {
                        auto sm = std::make_shared<Model>(meshes[metadata.SubMeshIndex]);
                        // Propagate parent's root transform (SwapYZ, GlobalScale) for correct preview orientation
                        sm->GetRootNode().LocalTransform = parent->GetRootNode().LocalTransform;
                        asset = sm;
                    }
                }
            }
            // Cache SubMesh in s_Assets so repeated GetAsset calls don't reload parent
            if (asset) s_Assets[handle] = asset;
            return asset;
        }
        else if (metadata.Type == AssetType::Model) {
            // 【核心修复】：拦截内置几何体，防止传入 Assimp
            if (metadata.VirtualPath == "Primitive::Cube") {
                asset = std::make_shared<Model>(Mesh::CreateCube(1.0f));
            }
            else if (metadata.VirtualPath == "Primitive::Sphere") {
                asset = std::make_shared<Model>(Mesh::CreateSphere(0.5f, 64, 64));
            }
            else if (metadata.VirtualPath == "Primitive::Plane") {
                asset = std::make_shared<Model>(Mesh::CreatePlane(1.0f, 1.0f));
            }
            else {
                // 只有真正的硬盘路径 (如 .obj, .fbx) 才调用 Model 构造函数交给 Assimp
                asset = std::make_shared<Model>(physicalPath, metadata.ModelSettings);
            }
        }
        else if (metadata.Type == AssetType::Material) {
            auto material = std::make_shared<Material>();
            if (MaterialSerializer::Deserialize(material, physicalPath)) {
                asset = material;
            }
        }
        else if (metadata.Type == AssetType::Prefab) {
            auto prefab = std::make_shared<Prefab>();
            if (prefab->Load(physicalPath)) {
                asset = prefab;
            }
        }
        else if (metadata.Type == AssetType::TextureCube) {
            asset = TextureCube::Create(physicalPath);
        }
        else if (metadata.Type == AssetType::Scene) {
            // Scenes are not loaded as GPU/memory objects.
            // They are deserialized directly by EditorLayer::OpenSceneFile().
            return nullptr;
        }

        // LuaScript 类型的资源实际上不需要"加载到内存生成 C++ 对象"，
        // 脚本引擎在运行时直接调用 GetAssetPhysicalPath 去读文本文件即可，所以这里不需要做处理。

        if (asset) {
            // 将读取成功的资产存入内存池，下次就不需要再读硬盘了
            s_Assets[handle] = asset;
        } else if (metadata.Type != AssetType::LuaScript) {
            AYAYA_CORE_ERROR("AssetManager: Failed to load asset from path '{0}'", physicalPath);
        }

        return asset;
    }

    // =====================================================================
    // 异步预加载：后台线程 CPU 加载 + 主线程回调 GPU 上传
    // 适用场景：ContentBrowser 浏览目录时提前预热纹理，避免拖拽大文件卡 UI
    // =====================================================================
    void AssetManager::RequestAsyncLoad(UUID handle) {
        // 已加载到内存，无需重复加载
        if (s_Assets.find(handle) != s_Assets.end()) {
            AYAYA_CORE_TRACE("[Async] Asset {0} already in memory, skip", (uint64_t)handle);
            return;
        }

        const auto& metadata = s_Registry[handle];
        if (metadata.Type != AssetType::Texture2D && metadata.Type != AssetType::Model) return;

        // 防重入：同一资产不重复开启后台线程
        {
            std::lock_guard<std::mutex> lock(s_LoadingMutex);
            if (s_LoadingAssets.find(handle) != s_LoadingAssets.end()) {
                AYAYA_CORE_TRACE("[Async] Asset {0} already loading, skip", (uint64_t)handle);
                return;
            }
            s_LoadingAssets.insert(handle);
        }

        std::string physicalPath = VFS::ResolveString(metadata.VirtualPath);
        AssetType type = metadata.Type;
        auto vpath = metadata.VirtualPath;
        TextureImportSettings texSettings = metadata.TextureSettings;

        AYAYA_CORE_INFO("[Async] Spawning bg thread for asset {0} ({1})", (uint64_t)handle, physicalPath);

        // 后台线程：执行纯 CPU 密集任务（stbi_load 读取解压、Assimp 解析顶点）
        // GPU 资源创建（VkImage、VkBuffer）通过 SubmitToMainThread 回到主线程执行
        std::thread([handle, type, physicalPath, vpath, texSettings]() {
            try {
                if (type == AssetType::Texture2D) {
                    AYAYA_CORE_INFO("[Async] BG: stbi_load start for {0}", physicalPath);
                    auto raw = Texture2D::LoadRawDataFromDisk(physicalPath);
                    raw.ImportSettings = texSettings;
                    if (raw.Pixels) {
                        AYAYA_CORE_INFO("[Async] BG: stbi_load done ({0}x{1}), queue GPU upload", raw.Width, raw.Height);
                        auto rawPtr = std::make_shared<RawTextureData>(std::move(raw));
                        SubmitToMainThread([handle, rawPtr]() {
                            auto tex = Texture2D::CreateFromRawData(*rawPtr);
                            if (tex) {
                                s_Assets[handle] = tex;
                                AYAYA_CORE_INFO("[Async] MAIN: GPU upload done, asset {0} ready", (uint64_t)handle);
                            }
                        });
                    } else {
                        AYAYA_CORE_ERROR("[Async] BG: stbi_load FAILED for {0}", physicalPath);
                    }
                }
                else if (type == AssetType::Model && vpath.rfind("Primitive::", 0) == 0) {
                    SubmitToMainThread([handle, vpath]() {
                        std::shared_ptr<Model> model;
                        if (vpath == "Primitive::Cube")       model = std::make_shared<Model>(Mesh::CreateCube(1.0f));
                        else if (vpath == "Primitive::Sphere") model = std::make_shared<Model>(Mesh::CreateSphere(0.5f, 64, 64));
                        else if (vpath == "Primitive::Plane") model = std::make_shared<Model>(Mesh::CreatePlane(1.0f, 1.0f));
                        if (model) {
                            s_Assets[handle] = model;
                            AYAYA_CORE_INFO("[Async] MAIN: Built-in model {0} ready", (uint64_t)handle);
                        }
                    });
                }
            } catch (const std::exception& e) {
                AYAYA_CORE_ERROR("[Async] Exception for {0}: {1}", physicalPath, e.what());
            }

            SubmitToMainThread([handle]() {
                std::lock_guard<std::mutex> lock(s_LoadingMutex);
                s_LoadingAssets.erase(handle);
                AYAYA_CORE_TRACE("[Async] Asset {0} removed from loading set", (uint64_t)handle);
            });
        }).detach();
    }

    // =====================================================================
    // 内置单例资产获取
    // =====================================================================
    // =====================================================================
    // 核心修复：内置单例资产获取 (永久绑定 UUID 并自动注册)
    // =====================================================================
    UUID AssetManager::GetBuiltInCube() {
        static constexpr uint64_t CUBE_UUID = 16140901000000000001ull;
        UUID handle = CUBE_UUID;
        if (s_Assets.find(handle) == s_Assets.end()) {
            auto cube = std::make_shared<Model>(Mesh::CreateCube(1.0f));
            AddAsset(handle, cube);
        }
        return handle;
    }

    UUID AssetManager::GetBuiltInSphere() {
        static constexpr uint64_t SPHERE_UUID = 16140901000000000002ull;
        UUID handle = SPHERE_UUID;
        if (s_Assets.find(handle) == s_Assets.end()) {
            auto sphere = std::make_shared<Model>(Mesh::CreateSphere(0.5f, 64, 64));
            AddAsset(handle, sphere);
        }
        return handle;
    }

    UUID AssetManager::GetBuiltInPlane() {
        static constexpr uint64_t PLANE_UUID = 16140901000000000003ull;
        UUID handle = PLANE_UUID;
        if (s_Assets.find(handle) == s_Assets.end()) {
            auto plane = std::make_shared<Model>(Mesh::CreatePlane(1.0f, 1.0f));
            AddAsset(handle, plane);
        }
        return handle;
    }

    UUID AssetManager::GetBuiltInMaterial() {
        static constexpr uint64_t MAT_UUID = 16140901000000000004ull;
        UUID handle = MAT_UUID;
        if (s_Assets.find(handle) == s_Assets.end()) {
            auto mat = std::make_shared<Material>();
            std::string defaultMatPath = VFS::ResolveString("engine://Editor/materials/DefaultPBR.mat");
            if (MaterialSerializer::Deserialize(mat, defaultMatPath)) {
                mat->Name = "Built-in Default PBR";
            } else {
                mat->Name = "Built-in Fallback Material";
            }
            AddAsset(handle, mat);
        }
        return handle;
    }

    UUID AssetManager::GetBuiltInMaterialInstance() {
        // Clone the built-in material so each entity owns an independent copy.
        // Edits to one entity's material never leak to another.
        auto baseMat = GetAsset<Material>(GetBuiltInMaterial());
        auto clone = baseMat ? baseMat->Clone() : std::make_shared<Material>();
        clone->Name = "Material";  // clean name — user can rename via "Save to .mat"
        UUID newHandle = UUID();
        AddAsset(newHandle, clone);
        return newHandle;
    }

    // =====================================================================
    // 根据物理路径查找已注册资产的 UUID
    // =====================================================================
    UUID AssetManager::FindHandleForPath(const std::filesystem::path& filepath) {
        std::string virtualPath = VFS::GetVirtualPath(filepath);
        UUID subMeshFallback = 0;
        for (const auto& [handle, metadata] : s_Registry) {
            if (metadata.VirtualPath == virtualPath) {
                // Prefer parent Model over SubMesh — SubMesh entries share the same VirtualPath
                // as their parent, but only the parent has the actual file and import settings.
                if (metadata.Type == AssetType::SubMesh) {
                    if (subMeshFallback == 0) subMeshFallback = handle;
                    continue;
                }
                return handle;
            }
        }
        return subMeshFallback;  // only SubMesh entries found — return first one as fallback
    }

    // =====================================================================
    // 实用工具
    // =====================================================================
    bool AssetManager::IsAssetHandleValid(UUID handle) {
        return handle != 0 && (s_Assets.count(handle) || s_Registry.count(handle));
    }

    std::string AssetManager::GetAssetPhysicalPath(UUID handle) {
        if (s_Registry.find(handle) != s_Registry.end()) {
            return VFS::ResolveString(s_Registry[handle].VirtualPath);
        }
        return "";
    }

    // =====================================================================
    // 工业级防御的账本序列化 (保存)
    // =====================================================================
    void AssetManager::SerializeRegistry(const std::string& path) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "AssetRegistry" << YAML::Value << YAML::BeginSeq;

        int savedCount = 0;
        int dupesSkipped = 0;
        std::unordered_set<std::string> seenPaths;
        for (const auto& [handle, metadata] : s_Registry) {
            if (metadata.VirtualPath.empty() || metadata.Type == AssetType::None) continue;
            // 去重：同一 VirtualPath 只保留第一个 UUID
            if (seenPaths.count(metadata.VirtualPath)) {
                dupesSkipped++;
                continue;
            }
            seenPaths.insert(metadata.VirtualPath);

            out << YAML::BeginMap;
            out << YAML::Key << "Handle" << YAML::Value << (uint64_t)handle;
            out << YAML::Key << "Type" << YAML::Value << (int)metadata.Type;
            out << YAML::Key << "VirtualPath" << YAML::Value << metadata.VirtualPath;
            out << YAML::EndMap;
            savedCount++;
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;

        std::ofstream fout(path);
        if (!fout.is_open()) {
            AYAYA_CORE_ERROR("AssetManager: Failed to open registry file for writing: {0}", path);
            return;
        }

        fout << out.c_str();
        fout.close();
        if (dupesSkipped > 0)
            AYAYA_CORE_WARN("AssetManager: Registry saved to {0} ({1} assets, {2} duplicates skipped).", path, savedCount, dupesSkipped);
        else
            AYAYA_CORE_TRACE("AssetManager: Registry saved to {0} ({1} assets).", path, savedCount);
    }

    // =====================================================================
    // 工业级防御的账本反序列化 (读取)
    // =====================================================================
    bool AssetManager::DeserializeRegistry(const std::string& path) {
        if (!std::filesystem::exists(path)) {
            AYAYA_CORE_WARN("AssetManager: Registry file does not exist. It will be created. Path: {0}", path);
            return false;
        }

        YAML::Node data;
        try {
            data = YAML::LoadFile(path);
        }
        catch (const YAML::ParserException& e) {
            AYAYA_CORE_ERROR("AssetManager: Failed to parse AssetRegistry.yaml! Error: {0}", e.what());
            return false;
        }

        auto registryNode = data["AssetRegistry"];
        if (!registryNode || !registryNode.IsSequence()) {
            AYAYA_CORE_ERROR("AssetManager: AssetRegistry.yaml is corrupted or empty!");
            return false;
        }

        // 加载新账本前彻底清空旧账本
        s_Registry.clear();
        int loadedCount = 0;
        int dupesSkipped = 0;
        std::unordered_map<std::string, UUID> seenPaths; // VirtualPath → UUID 去重

        for (auto assetNode : registryNode) {
            try {
                UUID handle = assetNode["Handle"].as<uint64_t>();
                AssetType type = (AssetType)assetNode["Type"].as<int>();
                std::string virtualPath = assetNode["VirtualPath"].as<std::string>();

                // 去重：同一 VirtualPath 只保留首次出现的 UUID
                auto it = seenPaths.find(virtualPath);
                if (it != seenPaths.end()) {
                    AYAYA_CORE_WARN("AssetManager: Duplicate VirtualPath '{0}' — keeping UUID {1}, discarding {2}",
                        virtualPath, (uint64_t)it->second, (uint64_t)handle);
                    dupesSkipped++;
                    continue;
                }
                seenPaths[virtualPath] = handle;
                s_Registry[handle] = { type, virtualPath };
                loadedCount++;
            }
            catch (const std::exception& e) {
                AYAYA_CORE_WARN("AssetManager: Failed to parse a registry entry. Error: {0}", e.what());
                continue;
            }
        }

        if (dupesSkipped > 0)
            AYAYA_CORE_WARN("AssetManager: Loaded {0} assets from registry ({1} duplicates removed).", loadedCount, dupesSkipped);
        else
            AYAYA_CORE_INFO("AssetManager: Successfully loaded {0} assets from registry.", loadedCount);
        return true;
    }

    void AssetManager::RegisterDependency(UUID dependent, UUID dependency) {
        s_ReverseDeps[dependency].insert(dependent);
    }

    const std::unordered_set<UUID>& AssetManager::GetDependents(UUID handle) {
        static std::unordered_set<UUID> empty;
        auto it = s_ReverseDeps.find(handle);
        if (it != s_ReverseDeps.end()) return it->second;
        return empty;
    }

    // =====================================================================
    // File system mutation APIs
    // =====================================================================

    bool AssetManager::DeleteAsset(UUID handle) {
        auto it = s_Registry.find(handle);
        if (it == s_Registry.end()) return false;

        AssetType delType = it->second.Type;

        // SubMesh assets share the parent Model's file — never move physical files
        if (delType != AssetType::SubMesh) {
            std::string physPath = VFS::ResolveString(it->second.VirtualPath);
            std::filesystem::path srcPath(physPath);
            std::filesystem::path metaPath(physPath + ".meta");

            if (!std::filesystem::exists(srcPath)) {
                if (std::filesystem::exists(metaPath))
                    std::filesystem::remove(metaPath);
            } else {
                auto trashDir = Project::GetProjectDirectory() / ".Trash";
                std::filesystem::create_directories(trashDir);

                auto destPath = trashDir / srcPath.filename();
                auto destMeta = trashDir / srcPath.filename().concat(".meta");

                int counter = 1;
                while (std::filesystem::exists(destPath)) {
                    auto numbered = srcPath.stem().string() + "_" + std::to_string(++counter) + srcPath.extension().string();
                    destPath = trashDir / numbered;
                    destMeta = trashDir / (numbered + ".meta");
                }

                std::error_code ec;
                std::filesystem::rename(srcPath, destPath, ec);
                if (std::filesystem::exists(metaPath))
                    std::filesystem::rename(metaPath, destMeta, ec);
            }
        }

        // If deleting a Model, also clean up all SubMesh children
        if (delType == AssetType::Model) {
            std::vector<UUID> children;
            for (auto& [subHandle, subMeta] : s_Registry)
                if (subMeta.Type == AssetType::SubMesh && subMeta.ParentHandle == handle)
                    children.push_back(subHandle);
            for (UUID child : children) {
                s_Registry.erase(child);
                auto ca = s_Assets.find(child);
                if (ca != s_Assets.end()) {
                    DeferredRelease dr;
                    dr.Asset = ca->second;
                    dr.FramesRemaining = 3;
                    s_DeferredReleases.push_back(std::move(dr));
                    s_Assets.erase(ca);
                }
            }
        }

        // Remove from registry; defer GPU resource destruction
        s_Registry.erase(it);
        auto itAsset = s_Assets.find(handle);
        if (itAsset != s_Assets.end()) {
            DeferredRelease dr;
            dr.Asset = itAsset->second;
            dr.FramesRemaining = 3;
            s_DeferredReleases.push_back(std::move(dr));
            s_Assets.erase(itAsset);
        }

        AYAYA_CORE_INFO("AssetManager: Deleted asset ({0})", (uint64_t)handle);
        return true;
    }

    bool AssetManager::RenameAsset(UUID handle, const std::string& newName) {
        auto it = s_Registry.find(handle);
        if (it == s_Registry.end()) return false;

        std::string oldPhys = VFS::ResolveString(it->second.VirtualPath);
        std::filesystem::path oldPath(oldPhys);
        std::filesystem::path oldMeta(oldPhys + ".meta");

        auto ext = oldPath.extension();
        std::filesystem::path newPath = oldPath.parent_path() / (newName + ext.string());
        std::filesystem::path newMeta = oldPath.parent_path() / (newName + ext.string() + ".meta");

        if (std::filesystem::exists(newPath)) {
            AYAYA_CORE_WARN("AssetManager: Rename failed — target exists: {0}", newPath.string());
            return false;
        }

        std::error_code ec;
        std::filesystem::rename(oldPath, newPath, ec);
        if (ec) {
            AYAYA_CORE_ERROR("AssetManager: Rename failed: {0}", ec.message());
            return false;
        }
        if (std::filesystem::exists(oldMeta)) {
            std::filesystem::rename(oldMeta, newMeta, ec);
        }

        // Update virtual path in registry
        std::string newVPath = VFS::GetVirtualPath(newPath);
        it->second.VirtualPath = newVPath;

        // Sync SubMesh children's VirtualPath
        for (auto& [subHandle, subMeta] : s_Registry) {
            if (subMeta.Type == AssetType::SubMesh && subMeta.ParentHandle == handle)
                subMeta.VirtualPath = newVPath;
        }

        // Rewrite .meta with new UUID→path binding
        RewriteMetaFile(handle);

        AYAYA_CORE_INFO("AssetManager: Renamed asset to {0}", newPath.filename().string());
        return true;
    }

    bool AssetManager::MoveAsset(UUID handle, const std::filesystem::path& destDir) {
        auto it = s_Registry.find(handle);
        if (it == s_Registry.end()) return false;

        std::string oldPhys = VFS::ResolveString(it->second.VirtualPath);
        std::filesystem::path oldPath(oldPhys);
        std::filesystem::path oldMeta(oldPhys + ".meta");

        std::filesystem::path newPath = destDir / oldPath.filename();
        std::filesystem::path newMeta = destDir / (oldPath.filename().string() + ".meta");

        if (std::filesystem::equivalent(oldPath.parent_path(), destDir))
            return true; // same directory, no-op

        if (std::filesystem::exists(newPath)) {
            AYAYA_CORE_WARN("AssetManager: Move failed — target exists: {0}", newPath.string());
            return false;
        }

        std::filesystem::create_directories(destDir);
        std::error_code ec;
        std::filesystem::rename(oldPath, newPath, ec);
        if (ec) {
            AYAYA_CORE_ERROR("AssetManager: Move failed: {0}", ec.message());
            return false;
        }
        if (std::filesystem::exists(oldMeta)) {
            std::filesystem::rename(oldMeta, newMeta, ec);
        }

        std::string newVPath = VFS::GetVirtualPath(newPath);
        it->second.VirtualPath = newVPath;

        // Sync SubMesh children's VirtualPath so they stay consistent with parent
        for (auto& [subHandle, subMeta] : s_Registry) {
            if (subMeta.Type == AssetType::SubMesh && subMeta.ParentHandle == handle)
                subMeta.VirtualPath = newVPath;
        }

        RewriteMetaFile(handle);

        AYAYA_CORE_INFO("AssetManager: Moved asset to {0}", newPath.string());
        return true;
    }

    bool AssetManager::CreateSceneAsset(const std::filesystem::path& destDir,
                                         const std::string& sceneName) {
        std::string name = sceneName;
        if (name.find(".ayaya") == std::string::npos)
            name += ".ayaya";

        std::string base = name.substr(0, name.size() - 6);  // strip ".ayaya"
        std::filesystem::path fullPath = destDir / name;

        // Deduplicate: append " 2", " 3", ...
        int suffix = 2;
        while (std::filesystem::exists(fullPath) && suffix < 100) {
            fullPath = destDir / (base + " " + std::to_string(suffix) + ".ayaya");
            suffix++;
        }
        if (std::filesystem::exists(fullPath)) {
            AYAYA_CORE_WARN("AssetManager: Scene already exists — too many duplicates: {0}", fullPath.string());
            return false;
        }

        std::filesystem::create_directories(destDir);

        // Create a default scene
        auto scene = std::make_shared<Scene>();
        {
            Entity cam = scene->CreateEntity("Main Camera");
            auto& cc = cam.AddComponent<CameraComponent>();
            cc.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
            cc.Camera.SetViewportSize(1280, 720);
            cam.GetComponent<TransformComponent>().Translation = {0.0f, 0.0f, 5.0f};
        }
        {
            Entity light = scene->CreateEntity("Directional Light");
            auto& dl = light.AddComponent<DirectionalLightComponent>();
            dl.Color = glm::vec3(1.0f);
            dl.Illuminance = 100000.0f;
            auto& lt = light.GetComponent<TransformComponent>();
            lt.Rotation = glm::vec3(glm::radians(45.0f), glm::radians(-45.0f), 0.0f);
        }

        // Serialize
        SceneSerializer serializer(scene);
        EditorState editorState;
        editorState.ShowGrid = true;
        editorState.CameraPosition = {0.0f, 0.0f, 5.0f};
        editorState.CameraDistance = 5.0f;
        editorState.CameraPitch = 0.0f;
        editorState.CameraYaw = 0.0f;
        editorState.CameraFocalPoint = {0.0f, 0.0f, 0.0f};
        serializer.Serialize(fullPath.string(), editorState);

        // Register as asset
        UUID handle = UUID();
        std::string vpath = VFS::GetVirtualPath(fullPath);
        WriteMetaFile(fullPath, handle, AssetType::Scene);
        s_Registry[handle] = {AssetType::Scene, vpath};

        AYAYA_CORE_INFO("AssetManager: Created scene asset {0}", fullPath.string());
        return true;
    }

    bool AssetManager::CreateFolder(const std::filesystem::path& parentDir,
                                     const std::string& folderName) {
        std::filesystem::path fullPath = parentDir / folderName;
        // Deduplicate: if already exists, append " 2", " 3", ...
        int suffix = 2;
        while (std::filesystem::exists(fullPath) && suffix < 100) {
            fullPath = parentDir / (folderName + " " + std::to_string(suffix));
            suffix++;
        }
        if (std::filesystem::exists(fullPath)) {
            AYAYA_CORE_WARN("AssetManager: CreateFolder failed — too many duplicates: {0}", fullPath.string());
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(fullPath, ec);
        if (ec) {
            AYAYA_CORE_ERROR("AssetManager: CreateFolder failed: {0}", ec.message());
            return false;
        }
        AYAYA_CORE_INFO("AssetManager: Created folder {0}", fullPath.string());
        return true;
    }

    void AssetManager::UnregisterAsset(UUID handle) {
        s_Registry.erase(handle);
        s_Assets.erase(handle);
    }

}
