#pragma once
#include "Asset.hpp"
#include <memory>
#include <unordered_map>

namespace Ayaya {

    // 新增：资产的元数据，记录它在硬盘上的真身
    struct AssetMetadata {
        AssetType Type = AssetType::None;
        std::filesystem::path FilePath;
    };

    class AssetManager {
    public:
        // 将已经加载好的资源托管给仓库
        static void AddAsset(const std::shared_ptr<Asset>& asset);

        // ==========================================
        // 新增：资产注册表核心 API
        // ==========================================
        static UUID ImportAsset(const std::filesystem::path& filepath);
        static void SerializeRegistry(const std::string& path);
        static bool DeserializeRegistry(const std::string& path);
        
        // ==========================================
        // 核心修复：完整的懒加载 (Lazy Load) 逻辑！
        // ==========================================
        template<typename T>
        static std::shared_ptr<T> GetAsset(UUID handle) {
            // 1. 如果内存池里有，极速返回
            if (s_Assets.find(handle) != s_Assets.end()) {
                return std::dynamic_pointer_cast<T>(s_Assets[handle]);
            }

            // 2. 内存没有？没关系！去硬盘账本里查！如果有记录，立刻读取！
            if (s_Registry.find(handle) != s_Registry.end()) {
                return std::dynamic_pointer_cast<T>(LoadAssetFromFile(handle));
            }

            // 3. 只有当内存和账本里都找不到时，才返回空指针
            return nullptr;
        }

        // 检查某个资源是否存在
        static bool IsAssetHandleValid(UUID handle);

        // 清空仓库缓存
        static void Clear();

    private:
        // 内部专用：真正执行硬盘读取的函数
        static std::shared_ptr<Asset> LoadAssetFromFile(UUID handle);

    private:
        // 仓库账本：UUID -> 资产指针
        static std::unordered_map<UUID, std::shared_ptr<Asset>> s_Assets;   // 内存池 (缓存)
        static std::unordered_map<UUID, AssetMetadata> s_Registry;          // 硬盘账本 (注册表)
    };

}