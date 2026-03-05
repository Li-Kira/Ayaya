#include "ayapch.h"
#include "AssetManager.hpp"
#include "Renderer/Texture.hpp" // 需要用来创建贴图
#include <yaml-cpp/yaml.h>
#include <fstream>

namespace Ayaya {

    std::unordered_map<UUID, std::shared_ptr<Asset>> AssetManager::s_Assets;
    std::unordered_map<UUID, AssetMetadata> AssetManager::s_Registry; // 初始化账本

    void AssetManager::AddAsset(const std::shared_ptr<Asset>& asset) {
        s_Assets[asset->Handle] = asset;
    }

    bool AssetManager::IsAssetHandleValid(UUID handle) {
        // 只要内存里有，或者账本里有记录，这个 Handle 就是合法的
        return s_Assets.find(handle) != s_Assets.end() || s_Registry.find(handle) != s_Registry.end();
    }

    void AssetManager::Clear() {
        s_Assets.clear(); 
        // 注意：Clear 只清空内存池，绝不能清空硬盘账本！
    }

    // ==========================================
    // 实现注册表相关方法
    // ==========================================
    UUID AssetManager::ImportAsset(const std::filesystem::path& filepath) {
        // 如果文件之前已经导入过，直接返回账本里记录的旧 UUID，防止重复生成
        for (const auto& [handle, metadata] : s_Registry) {
            if (metadata.FilePath == filepath) return handle;
        }

        UUID newHandle; 
        AssetMetadata metadata;
        metadata.FilePath = filepath;
        
        if (filepath.extension() == ".png" || filepath.extension() == ".jpg") {
            metadata.Type = AssetType::Texture2D;
        } else {
            return 0; // 不支持的类型
        }

        s_Registry[newHandle] = metadata;
        
        // 导入后立刻保存账本文件到硬盘，防止断电丢失
        SerializeRegistry("assets/AssetRegistry.yaml");

        return newHandle;
    }

    std::shared_ptr<Asset> AssetManager::LoadAssetFromFile(UUID handle) {
        const auto& metadata = s_Registry[handle];
        
        if (metadata.Type == AssetType::Texture2D) {
            // 利用账本里的路径，重新加载贴图
            auto texture = Texture2D::Create(metadata.FilePath.string());
            if (texture) {
                texture->Handle = handle; // 【核心！】：强制把贴图的 UUID 设回账本里的 UUID
                s_Assets[handle] = texture; // 塞进内存池
                return texture;
            }
        }
        return nullptr;
    }

    void AssetManager::SerializeRegistry(const std::string& path) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "AssetRegistry" << YAML::Value << YAML::BeginSeq;

        for (const auto& [handle, metadata] : s_Registry) {
            out << YAML::BeginMap;
            out << YAML::Key << "Handle" << YAML::Value << (uint64_t)handle;
            out << YAML::Key << "FilePath" << YAML::Value << metadata.FilePath.string();
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

        for (auto assetNode : registryNode) {
            UUID handle = assetNode["Handle"].as<uint64_t>();
            AssetMetadata metadata;
            metadata.FilePath = assetNode["FilePath"].as<std::string>();
            metadata.Type = (AssetType)assetNode["Type"].as<int>();

            s_Registry[handle] = metadata;
        }
        return true;
    }
}