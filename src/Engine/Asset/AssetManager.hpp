#pragma once

#include "Core/UUID.hpp"
#include "Asset.hpp"

#include <memory>
#include <unordered_map>
#include <filesystem>
#include <string>

namespace Ayaya {

    // ==========================================
    // 资产元数据：记录在硬盘注册表中的信息
    // ==========================================
    struct AssetMetadata {
        AssetType Type = AssetType::None;
        std::string VirtualPath; // 存储跨平台的虚拟路径 (如 project://... 或 engine://...)
    };

    class AssetManager {
    public:
        static void Init();
        static void Shutdown();
        static void Clear(); // 清空当前内存池和账本（切换场景/项目时使用）

        // ==========================================
        // 核心：泛型资产存取接口 (无侵入式设计)
        // ==========================================
        
        // 手动将一个运行时创建的对象推入资产池，并与 UUID 绑定
        template<typename T>
        static void AddAsset(UUID handle, const std::shared_ptr<T>& asset) {
            // std::shared_ptr<T> 会自动且安全地隐式转换为 std::shared_ptr<void>
            s_Assets[handle] = asset; 
        }

        // 懒加载核心：通过 UUID 换取真实的智能指针
        template<typename T>
        static std::shared_ptr<T> GetAsset(UUID handle) {
            // 1. 查内存池 (极速返回)
            if (s_Assets.find(handle) != s_Assets.end()) {
                // 将 void 智能指针安全地还原为目标类型
                return std::static_pointer_cast<T>(s_Assets[handle]);
            }

            // 2. 内存没有？查账本进行硬盘懒加载
            if (s_Registry.find(handle) != s_Registry.end()) {
                return std::static_pointer_cast<T>(LoadAssetFromFile(handle));
            }

            // 3. 全都没找到，返回空指针
            return nullptr;
        }

        // ==========================================
        // 资产注册表 API
        // ==========================================
        static UUID ImportAsset(const std::filesystem::path& filepath);
        static void SerializeRegistry(const std::string& path);
        static bool DeserializeRegistry(const std::string& path);
        
        // ==========================================
        // 内置单例资产 API (全局唯一，节省显存)
        // ==========================================
        static UUID GetBuiltInCube();
        static UUID GetBuiltInSphere();
        static UUID GetBuiltInPlane();
        static UUID GetBuiltInMaterial();

        // ==========================================
        // 实用工具接口
        // ==========================================
        static bool IsAssetHandleValid(UUID handle);
        
        // 根据 Handle 获取在当前电脑硬盘上的真实绝对路径 (专供 Lua 脚本引擎等底层读取使用)
        static std::string GetAssetPhysicalPath(UUID handle);

    private:
        // 内部专用：真正执行硬盘读取的函数，返回擦除了类型的 void 指针
        static std::shared_ptr<void> LoadAssetFromFile(UUID handle);

    private:
        // 【核心黑科技】：使用 void 擦除类型！完美接纳所有实体类，无需它们继承任何基类！
        static std::unordered_map<UUID, std::shared_ptr<void>> s_Assets;
        static std::unordered_map<UUID, AssetMetadata> s_Registry;

        // 内置单例资产的句柄缓存
        static UUID s_BuiltInCubeHandle;
        static UUID s_BuiltInSphereHandle;
        static UUID s_BuiltInPlaneHandle;
        static UUID s_BuiltInMaterialHandle;
    };

}