#include "ayapch.h"
#include "AssetManager.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/Model.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/MaterialSerializer.hpp"
#include "Core/VFS.hpp"
#include "Core/Log.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>

namespace Ayaya {

    // 静态成员初始化
    std::unordered_map<UUID, std::shared_ptr<void>> AssetManager::s_Assets;
    std::unordered_map<UUID, AssetMetadata> AssetManager::s_Registry;

    UUID AssetManager::s_BuiltInCubeHandle = 0;
    UUID AssetManager::s_BuiltInSphereHandle = 0;
    UUID AssetManager::s_BuiltInPlaneHandle = 0;
    UUID AssetManager::s_BuiltInMaterialHandle = 0;

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
    // 懒加载核心：根据账本信息，真正执行硬盘读取的地方
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

        // LuaScript 类型的资源实际上不需要“加载到内存生成 C++ 对象”，
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
    // 内置单例资产获取 (完美解决之前的报错)
    // =====================================================================
    UUID AssetManager::GetBuiltInCube() {
        if (s_BuiltInCubeHandle == 0) {
            s_BuiltInCubeHandle = UUID();
            auto cube = std::make_shared<Model>(Mesh::CreateCube(1.0f));
            AddAsset(s_BuiltInCubeHandle, cube); 
        }
        return s_BuiltInCubeHandle;
    }

    UUID AssetManager::GetBuiltInSphere() {
        if (s_BuiltInSphereHandle == 0) {
            s_BuiltInSphereHandle = UUID();
            auto sphere = std::make_shared<Model>(Mesh::CreateSphere(0.5f, 64, 64));
            AddAsset(s_BuiltInSphereHandle, sphere);
        }
        return s_BuiltInSphereHandle;
    }

    UUID AssetManager::GetBuiltInPlane() {
        if (s_BuiltInPlaneHandle == 0) {
            s_BuiltInPlaneHandle = UUID();
            auto plane = std::make_shared<Model>(Mesh::CreatePlane(1.0f, 1.0f));
            AddAsset(s_BuiltInPlaneHandle, plane);
        }
        return s_BuiltInPlaneHandle;
    }

    UUID AssetManager::GetBuiltInMaterial() {
        if (s_BuiltInMaterialHandle == 0) {
            s_BuiltInMaterialHandle = UUID();
            auto mat = std::make_shared<Material>();
            
            // 【核心改造】：尝试从 VFS 引擎目录读取真实的默认 PBR 材质
            std::string defaultMatPath = VFS::ResolveString("engine://Editor/materials/DefaultPBR.mat");
            
            if (MaterialSerializer::Deserialize(mat, defaultMatPath)) {
                // 读取成功，稍微改个名字以防混淆
                mat->Name = "Built-in Default PBR";
            } else {
                // 如果文件丢失，作为保底方案(Fallback)，给一个纯净的名字
                AYAYA_CORE_WARN("AssetManager: Failed to load DefaultPBR.mat, using empty fallback material.");
                mat->Name = "Built-in Fallback Material";
            }

            // 将读取好（或保底）的材质存入内存池
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