#include "ayapch.h"
#include "AssetManager.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/TextureCube.hpp"
#include "Renderer/Model.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/MaterialSerializer.hpp"
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
        // 1. 调用 VFS 将物理路径反推为带有协议的虚拟路径 (如 project://assets/...)
        std::string virtualPath = VFS::GetVirtualPath(filepath);

        // 2. 查重：防止同一个文件在账本中被分配多个 UUID
        for (const auto& [handle, metadata] : s_Registry) {
            if (metadata.VirtualPath == virtualPath) return handle;
        }

        // 3. 确定资产类型
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

        // 4. 注册到账本
        UUID newHandle = UUID();
        s_Registry[newHandle] = { type, virtualPath };

        // 自动保存项目账本 (默认保存在 project 挂载点的根目录)
        SerializeRegistry(VFS::ResolveString("project://AssetRegistry.yaml"));

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
        if (s_BuiltInCubeHandle == 0) {
            // 1. 先去账本里找，看以前有没有给 Cube 注册过固定户口
            for (const auto& [handle, metadata] : s_Registry) {
                if (metadata.VirtualPath == "Primitive::Cube") {
                    s_BuiltInCubeHandle = handle;
                    break;
                }
            }
            // 2. 如果是第一次分配，生成新 UUID 并强制写入项目账本
            if (s_BuiltInCubeHandle == 0) {
                s_BuiltInCubeHandle = UUID();
                s_Registry[s_BuiltInCubeHandle] = { AssetType::Model, "Primitive::Cube" };
                SerializeRegistry(VFS::ResolveString("project://AssetRegistry.yaml"));
            }
            // 3. 生成实体放入内存
            auto cube = std::make_shared<Model>(Mesh::CreateCube(1.0f));
            AddAsset(s_BuiltInCubeHandle, cube);
        }
        return s_BuiltInCubeHandle;
    }

    UUID AssetManager::GetBuiltInSphere() {
        if (s_BuiltInSphereHandle == 0) {
            for (const auto& [handle, metadata] : s_Registry) {
                if (metadata.VirtualPath == "Primitive::Sphere") {
                    s_BuiltInSphereHandle = handle;
                    break;
                }
            }
            if (s_BuiltInSphereHandle == 0) {
                s_BuiltInSphereHandle = UUID();
                s_Registry[s_BuiltInSphereHandle] = { AssetType::Model, "Primitive::Sphere" };
                SerializeRegistry(VFS::ResolveString("project://AssetRegistry.yaml"));
            }
            auto sphere = std::make_shared<Model>(Mesh::CreateSphere(0.5f, 64, 64));
            AddAsset(s_BuiltInSphereHandle, sphere);
        }
        return s_BuiltInSphereHandle;
    }

    UUID AssetManager::GetBuiltInPlane() {
        if (s_BuiltInPlaneHandle == 0) {
            for (const auto& [handle, metadata] : s_Registry) {
                if (metadata.VirtualPath == "Primitive::Plane") {
                    s_BuiltInPlaneHandle = handle;
                    break;
                }
            }
            if (s_BuiltInPlaneHandle == 0) {
                s_BuiltInPlaneHandle = UUID();
                s_Registry[s_BuiltInPlaneHandle] = { AssetType::Model, "Primitive::Plane" };
                SerializeRegistry(VFS::ResolveString("project://AssetRegistry.yaml"));
            }
            auto plane = std::make_shared<Model>(Mesh::CreatePlane(1.0f, 1.0f));
            AddAsset(s_BuiltInPlaneHandle, plane);
        }
        return s_BuiltInPlaneHandle;
    }

    UUID AssetManager::GetBuiltInMaterial() {
        if (s_BuiltInMaterialHandle == 0) {
            for (const auto& [handle, metadata] : s_Registry) {
                // 兼容带不带 Editor 的情况
                if (metadata.VirtualPath == "engine://materials/DefaultPBR.mat" ||
                    metadata.VirtualPath == "engine://Editor/materials/DefaultPBR.mat") {
                    s_BuiltInMaterialHandle = handle;
                    break;
                }
            }
            if (s_BuiltInMaterialHandle == 0) {
                s_BuiltInMaterialHandle = UUID();
                s_Registry[s_BuiltInMaterialHandle] = { AssetType::Material, "engine://Editor/materials/DefaultPBR.mat" };
                SerializeRegistry(VFS::ResolveString("project://AssetRegistry.yaml"));
            }

            auto mat = std::make_shared<Material>();
            std::string defaultMatPath = VFS::ResolveString("engine://Editor/materials/DefaultPBR.mat");

            if (MaterialSerializer::Deserialize(mat, defaultMatPath)) {
                mat->Name = "Built-in Default PBR";
            } else {
                mat->Name = "Built-in Fallback Material";
            }
            AddAsset(s_BuiltInMaterialHandle, mat);
        }
        return s_BuiltInMaterialHandle;
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
        for (const auto& [handle, metadata] : s_Registry) {
            if (metadata.VirtualPath.empty() || metadata.Type == AssetType::None) continue;

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

        for (auto assetNode : registryNode) {
            try {
                UUID handle = assetNode["Handle"].as<uint64_t>();
                AssetType type = (AssetType)assetNode["Type"].as<int>();
                std::string virtualPath = assetNode["VirtualPath"].as<std::string>();

                s_Registry[handle] = { type, virtualPath };
                loadedCount++;
            }
            catch (const std::exception& e) {
                AYAYA_CORE_WARN("AssetManager: Failed to parse a registry entry. Error: {0}", e.what());
                continue;
            }
        }

        AYAYA_CORE_INFO("AssetManager: Successfully loaded {0} assets from registry.", loadedCount);
        return true;
    }

}
