#include "ayapch.h"
#include "AssetManager.hpp"
#include "Renderer/Texture.hpp"
#include "Project/Project.hpp" 
#include "Core/VFS.hpp"        
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem> // 引入文件系统标准库

namespace Ayaya {

    std::unordered_map<UUID, std::shared_ptr<Asset>> AssetManager::s_Assets;
    std::unordered_map<UUID, AssetMetadata> AssetManager::s_Registry;

    void AssetManager::AddAsset(const std::shared_ptr<Asset>& asset) {
        s_Assets[asset->Handle] = asset;
    }

    bool AssetManager::IsAssetHandleValid(UUID handle) {
        return s_Assets.find(handle) != s_Assets.end() || s_Registry.find(handle) != s_Registry.end();
    }

    void AssetManager::Clear() {
        s_Assets.clear(); 
        s_Registry.clear(); 
    }

    void AssetManager::Shutdown() {
        s_Assets.clear();
        s_Registry.clear();
        AYAYA_CORE_INFO("AssetManager shutdown successfully.");
    }

    // ==========================================
    // 资产导入逻辑：支持项目相对路径转换
    // ==========================================
    UUID AssetManager::ImportAsset(const std::filesystem::path& filepath) {
        // 【修复编译错误】：直接使用 std::filesystem 计算相对路径
        std::filesystem::path relativePath;
        std::error_code ec;
        
        // 获取文件的绝对路径和项目 Assets 目录的绝对路径
        std::filesystem::path absFilepath = std::filesystem::absolute(filepath, ec);
        std::filesystem::path absAssetDir = std::filesystem::absolute(Project::GetAssetDirectory(), ec);
        
        // 计算相对路径 (例如从 D:/Game/Assets/textures/a.png 算出 textures/a.png)
        relativePath = std::filesystem::relative(absFilepath, absAssetDir, ec);

        // 防御性编程：如果计算失败，或者文件根本不在 Assets 目录下（路径带有 ".." 开头）
        if (ec || relativePath.empty() || relativePath.string().find("..") == 0) {
            AYAYA_CORE_WARN("AssetManager: Imported asset is outside of Project Asset Directory: {0}", filepath.string());
            // 兜底：直接使用原始路径格式化后的结果
            relativePath = filepath.lexically_normal(); 
        }

        // 查重：如果相对路径已存在，直接返回旧 Handle
        for (const auto& [handle, metadata] : s_Registry) {
            if (metadata.FilePath == relativePath) return handle;
        }

        UUID newHandle; 
        AssetMetadata metadata;
        metadata.FilePath = relativePath; // 存储相对路径
        
        // 识别类型
        std::string ext = relativePath.extension().string();
        if (ext == ".png" || ext == ".jpg" || ext == ".tga" || ext == ".hdr") {
            metadata.Type = AssetType::Texture2D;
        } else {
            AYAYA_CORE_WARN("AssetManager: Unsupported asset format '{0}'", ext);
            return 0;
        }

        s_Registry[newHandle] = metadata;
        
        // 保存到项目专属的账本文件
        SerializeRegistry(VFS::ResolveString("project://AssetRegistry.yaml"));

        return newHandle;
    }

    // ==========================================
    // 懒加载逻辑：物理路径解析
    // ==========================================
    std::shared_ptr<Asset> AssetManager::LoadAssetFromFile(UUID handle) {
        const auto& metadata = s_Registry[handle];
        
        // 将相对路径与项目根路径拼接，得到当前电脑上的绝对路径
        std::filesystem::path physicalPath = Project::GetAssetDirectory() / metadata.FilePath;

        if (metadata.Type == AssetType::Texture2D) {
            auto texture = Texture2D::Create(physicalPath.string());
            if (texture) {
                texture->Handle = handle; 
                s_Assets[handle] = texture;
                return texture;
            }
        }
        
        AYAYA_CORE_ERROR("AssetManager: Failed to load physical asset at {0}", physicalPath.string());
        return nullptr;
    }

    void AssetManager::SerializeRegistry(const std::string& path) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "AssetRegistry" << YAML::Value << YAML::BeginSeq;

        for (const auto& [handle, metadata] : s_Registry) {
            out << YAML::BeginMap;
            out << YAML::Key << "Handle" << YAML::Value << (uint64_t)handle;
            // 这里确保存储的是规范的通用路径字符串 (使用 / 替代 \)
            out << YAML::Key << "FilePath" << YAML::Value << metadata.FilePath.generic_string();
            out << YAML::Key << "Type" << YAML::Value << (int)metadata.Type;
            out << YAML::EndMap;
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;

        std::ofstream fout(path);
        fout << out.c_str();
    }

    bool AssetManager::DeserializeRegistry(const std::string& path) {
        if (!std::filesystem::exists(path)) return false;

        YAML::Node data;
        try { data = YAML::LoadFile(path); } 
        catch (YAML::ParserException e) { return false; }

        auto registryNode = data["AssetRegistry"];
        if (!registryNode) return false;

        // 加载新账本前清空旧账本
        s_Registry.clear();

        for (auto assetNode : registryNode) {
            UUID handle = assetNode["Handle"].as<uint64_t>();
            AssetMetadata metadata;
            metadata.FilePath = std::filesystem::path(assetNode["FilePath"].as<std::string>());
            metadata.Type = (AssetType)assetNode["Type"].as<int>();

            s_Registry[handle] = metadata;
        }
        
        AYAYA_CORE_INFO("AssetManager: Loaded {0} assets from registry.", s_Registry.size());
        return true;
    }
}