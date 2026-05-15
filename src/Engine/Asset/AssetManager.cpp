#include "ayapch.h"
#include "AssetManager.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/TextureCube.hpp"
#include "Renderer/Model.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/MaterialSerializer.hpp"
#include "Project/Project.hpp"
#include "Core/VFS.hpp"
#include "Core/Log.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <thread> // 异步加载

namespace Ayaya {

    // 静态成员初始化
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

    void AssetManager::Init() {
        AYAYA_CORE_INFO("AssetManager Initialized.");
    }

    void AssetManager::Shutdown() {
        Clear();
        AYAYA_CORE_INFO("AssetManager Shutdown.");
    }

    void AssetManager::Clear() {
        s_Assets.clear();
        s_Registry.clear();
    }

    // =====================================================================
    // .meta 文件系统 — 核心原语
    // =====================================================================

    bool AssetManager::WriteMetaFile(const std::filesystem::path& assetPhysicalPath, UUID handle, AssetType type) {
        std::filesystem::path metaPath = assetPhysicalPath.string() + ".meta";
        try {
            YAML::Emitter out;
            out << YAML::BeginMap;
            out << YAML::Key << "uuid" << YAML::Value << static_cast<uint64_t>(handle);
            out << YAML::Key << "type" << YAML::Value << static_cast<int>(type);
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
    void AssetManager::RefreshRegistry() {
        s_Registry.clear();
        auto assetDir = Project::GetAssetDirectory();

        if (std::filesystem::exists(assetDir)) {
            try {
                for (auto& entry : std::filesystem::recursive_directory_iterator(assetDir)) {
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
                        if (!ReadMetaFile(entry.path(), handle, type))
                            continue;

                        std::string vPath = VFS::GetVirtualPath(sourcePath);
                        s_Registry[handle] = { type, vPath };

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
            s_Assets.erase(handle);
            AYAYA_CORE_INFO("GC: Unloaded unused asset {0}", (uint64_t)handle);
        }

        if (!toErase.empty())
            AYAYA_CORE_INFO("GC: Sweep complete — {0} assets freed", toErase.size());
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
    }

    // =====================================================================
    // 资产导入逻辑：利用 VFS 智能识别项目还是引擎，并分配 UUID
    // =====================================================================
    UUID AssetManager::ImportAsset(const std::filesystem::path& filepath) {
        std::string virtualPath = VFS::GetVirtualPath(filepath);

        // 检查是否已经导入过（通过查找是否存在 .meta 文件）
        if (std::filesystem::exists(filepath.string() + ".meta")) {
            return FindHandleForPath(filepath);
        }

        // 查重：防止同一个文件在内存账本中被分配多个 UUID
        for (const auto& [handle, metadata] : s_Registry) {
            if (metadata.VirtualPath == virtualPath) return handle;
        }

        // 确定资产类型
        AssetType type = AssetType::None;
        std::string ext = filepath.extension().string();
        if (ext == ".png" || ext == ".jpg" || ext == ".hdr") type = AssetType::Texture2D;
        else if (ext == ".obj" || ext == ".fbx")             type = AssetType::Model;
        else if (ext == ".mat")                              type = AssetType::Material;
        else if (ext == ".lua")                              type = AssetType::LuaScript;
        else if (ext == ".cube")                             type = AssetType::TextureCube;

        if (type == AssetType::None) {
            AYAYA_CORE_WARN("AssetManager: Unsupported asset format '{0}'", ext);
            return 0;
        }

        UUID newHandle = UUID();

        // 写入独立的 .meta 文件（引擎资产和内置几何体不写 .meta）
        bool isEngineAsset = (virtualPath.rfind("engine://", 0) == 0) ||
                             (virtualPath.rfind("Primitive::", 0) == 0);
        if (!isEngineAsset) {
            WriteMetaFile(filepath, newHandle, type);
        }

        // 注册到内存账本
        s_Registry[newHandle] = { type, virtualPath };

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
                asset = std::make_shared<Model>(physicalPath);
            }
        }
        else if (metadata.Type == AssetType::Material) {
            auto material = std::make_shared<Material>();
            if (MaterialSerializer::Deserialize(material, physicalPath)) {
                asset = material;
            }
        }
        else if (metadata.Type == AssetType::TextureCube) {
            // 调用我们接下来要写的单文件重载函数
            asset = TextureCube::Create(physicalPath);
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

        AYAYA_CORE_INFO("[Async] Spawning bg thread for asset {0} ({1})", (uint64_t)handle, physicalPath);

        // 后台线程：执行纯 CPU 密集任务（stbi_load 读取解压、Assimp 解析顶点）
        // GPU 资源创建（VkImage、VkBuffer）通过 SubmitToMainThread 回到主线程执行
        std::thread([handle, type, physicalPath, vpath]() {
            try {
                if (type == AssetType::Texture2D) {
                    AYAYA_CORE_INFO("[Async] BG: stbi_load start for {0}", physicalPath);
                    auto raw = Texture2D::LoadRawDataFromDisk(physicalPath);
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

    // =====================================================================
    // 根据物理路径查找已注册资产的 UUID
    // =====================================================================
    UUID AssetManager::FindHandleForPath(const std::filesystem::path& filepath) {
        std::string virtualPath = VFS::GetVirtualPath(filepath);
        for (const auto& [handle, metadata] : s_Registry) {
            if (metadata.VirtualPath == virtualPath) return handle;
        }
        return 0;
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

}
