#pragma once

#include "Core/UUID.hpp"
#include "Asset.hpp"
#include "AssetSettings.hpp"

#include <atomic>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <string>
#include <functional>
#include <mutex>
#include <queue>

namespace Ayaya {

    // ==========================================
    // 资产元数据：记录在硬盘注册表中的信息
    // ==========================================
    struct AssetMetadata {
        AssetType Type = AssetType::None;
        std::string VirtualPath; // 存储跨平台的虚拟路径 (如 project://... 或 engine://...)
        TextureImportSettings TextureSettings;
        ModelImportSettings   ModelSettings;
        UUID ParentHandle = 0;            // for SubAssets: points to the parent ModelSource
        int  SubMeshIndex   = -1;         // for SubAssets: which mesh in the parent
    };

    // Result of a background-thread model import — plain data only, no GPU/registry state.
    // The background thread fills this, then SubmitToMainThread hands it to FinalizeModelImport.
    struct ImportResult {
        bool Success = false;
        std::string ErrorMsg;
        UUID ModelHandle = 0;
        std::string ModelVirtualPath;
        // Sub-assets generated during import
        struct MeshEntry { UUID Handle; std::string VirtualPath; std::string PhysicalPath; int SubMeshIndex = -1; std::string Name; };
        struct MatEntry  { UUID Handle; std::string VirtualPath; std::string PhysicalPath; int AssimpMaterialIndex = -1; };
        struct TexEntry  { std::string PhysicalPath; UUID Handle = 0; };
        std::vector<MeshEntry> SubMeshes;
        std::vector<MatEntry>  Materials;
        std::vector<TexEntry>  CopiedTextures;
        UUID PrefabHandle = 0;
        std::string PrefabPath;
    };

    class AssetManager {
    public:
        static void Init();
        static void Shutdown();
        static void Clear(); // 清空当前内存池和账本（切换场景/项目时使用）

        // ==========================================
        // 异步加载：引擎主循环每帧调用，处理后台线程提交的 GPU 上传回调
        // ==========================================
        static void Update();

        // 提交任务到主线程安全执行（供后台线程调用，通常用于 GPU 资源创建）
        static void SubmitToMainThread(const std::function<void()>& function);

        // 异步预加载：后台线程执行 CPU 密集任务（stbi_load），
        // 主线程回调执行 GPU 上传（VkImage/VkBuffer），不阻塞调用方
        static void RequestAsyncLoad(UUID handle);

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

        // Enhanced model import with user-configurable settings.
        // ImportModelAssetSync is CPU-heavy, background-thread safe — NO registry/GPU writes.
        // FinalizeModelImport runs on main thread via SubmitToMainThread — writes .meta, registers assets.
        static ImportResult ImportModelAssetSync(const std::filesystem::path& filepath,
                                                  const ModelImportSettings& settings);
        static void FinalizeModelImport(const ImportResult& result);
        static void SerializeRegistry(const std::string& path);
        static bool DeserializeRegistry(const std::string& path);

        // ==========================================
        // .meta 文件系统
        // ==========================================

        // 递归扫描项目资产目录下所有 .meta 文件，重建 s_Registry
        static void RefreshRegistry();

        // 读写单个 .meta 侧车文件
        static bool WriteMetaFile(const std::filesystem::path& assetPhysicalPath, UUID handle, AssetType type);
        static bool WriteMetaFile(const std::filesystem::path& assetPhysicalPath, UUID handle, AssetType type, const TextureImportSettings& settings);
        static bool ReadMetaFile(const std::filesystem::path& metaPath, UUID& outHandle, AssetType& outType);
        static bool ReadMetaFile(const std::filesystem::path& metaPath, UUID& outHandle, AssetType& outType, TextureImportSettings* outSettings, ModelImportSettings* outModelSettings = nullptr);

        // 一次性迁移：读取旧 AssetRegistry.yaml，为每个条目创建 .meta 文件
        static bool MigrateFromRegistry(const std::string& registryPath);

        // 从 engine://Editor/EngineAssets.yaml 加载引擎内部资产注册表
        static void LoadEngineAssets();

        // Mark & Sweep 垃圾回收：卸载所有不在 activeHandles 中的非内置资产
        static void UnloadUnusedAssets(const std::unordered_set<UUID>& activeHandles);

        // 更新资产的导入设置并覆写 .meta 文件
        static void UpdateMetadataSettings(UUID handle, const TextureImportSettings& settings);
        static void UpdateMetadataSettings(UUID handle, const ModelImportSettings& settings);

        // 强制重新加载资产（卸载旧纹理 + 异步重新加载）
        static void ReloadAsset(UUID handle);

        // ==========================================
        // 内置单例资产 API (全局唯一，节省显存)
        // ==========================================
        static UUID GetBuiltInCube();
        static UUID GetBuiltInSphere();
        static UUID GetBuiltInPlane();
        static UUID GetBuiltInMaterial();
        // Clone the built-in material into a per-object instance with a fresh UUID.
        // Each entity gets its own copy so edits don't leak across objects.
        static UUID GetBuiltInMaterialInstance();

        // 根据物理文件路径查找已注册的资产 UUID（未注册返回 0）
        static UUID FindHandleForPath(const std::filesystem::path& filepath);

        // Asset dependency graph (for cascade hot-reload)
        static void RegisterDependency(UUID dependent, UUID dependency);
        static const std::unordered_set<UUID>& GetDependents(UUID handle);

        // ==========================================
        // 实用工具接口
        // ==========================================
        static bool IsAssetHandleValid(UUID handle);

        // 根据 Handle 获取在当前电脑硬盘上的真实绝对路径 (专供 Lua 脚本引擎等底层读取使用)
        static std::string GetAssetPhysicalPath(UUID handle);

        // 获取当前内存中所有已加载的资产
        static const std::unordered_map<UUID, std::shared_ptr<void>>& GetLoadedAssets() { return s_Assets; }
        // 获取完整的资产注册表 (所有已注册资产，含未加载的)
        static const std::unordered_map<UUID, AssetMetadata>& GetRegistry() { return s_Registry; }
        // 更新已注册资产的虚拟路径（如将引擎默认材质克隆到项目后重定向）
        static void UpdateAssetPath(UUID handle, const std::string& newVirtualPath) {
            if (s_Registry.count(handle)) s_Registry[handle].VirtualPath = newVirtualPath;
        }
        // 获取指定 Handle 的元数据（用于判断类型）
        static AssetMetadata GetMetadata(UUID handle) {
            if (s_Registry.count(handle)) return s_Registry[handle];
            return {};
        }

        // ── Bulk import guard: suppress AssetWatcher reloads during glTF import ──
        // Prevents the file watcher from triggering synchronous LoadAssetFromFile
        // for every newly-created texture/material/prefab while import is writing files.
        static bool IsBulkImportInProgress() { return s_BulkImportInProgress.load(std::memory_order_acquire); }
        static void SetBulkImportInProgress(bool v) { s_BulkImportInProgress.store(v, std::memory_order_release); }

        // ==========================================
        // File system mutation APIs (ContentBrowser)
        // ==========================================

        // Move source file + .meta into <ProjectDir>/.Trash/
        static bool DeleteAsset(UUID handle);

        // Rename source file + .meta on disk, update VirtualPath in registry
        static bool RenameAsset(UUID handle, const std::string& newName);

        // Move source file + .meta to a new directory
        static bool MoveAsset(UUID handle, const std::filesystem::path& destDir);

        // Create an empty .ayaya scene file with default entities, .meta, and registry entry
        static bool CreateSceneAsset(const std::filesystem::path& destDir,
                                     const std::string& sceneName);

        // Create a physical folder (no UUID, no .meta)
        static bool CreateFolder(const std::filesystem::path& parentDir,
                                 const std::string& folderName);

        // Remove from registry & memory (no disk changes) — used when .meta is deleted externally
        static void UnregisterAsset(UUID handle);

        // Register a SubMesh asset in the registry with parent linkage.
        // Writes sub_assets YAML entries into the parent model's .meta file.
        static void RegisterSubMesh(UUID subHandle, UUID parentHandle, int subMeshIndex,
                                     const std::string& virtualPath);
        // Register a texture in s_Registry WITHOUT triggering async GPU upload.
        // Used for batch imports (glTF) to avoid OOM from concurrent stbi threads.
        static void RegisterTextureAsset(UUID handle, const std::string& physicalPath);

    private:
        // 内部专用：真正执行硬盘读取的函数，返回擦除了类型的 void 指针
        static std::shared_ptr<void> LoadAssetFromFile(UUID handle);
        // 覆写 .meta 文件，将当前注册表中的元数据刷新到磁盘
        static void RewriteMetaFile(UUID handle);

    private:
        // 【核心黑科技】：使用 void 擦除类型！完美接纳所有实体类，无需它们继承任何基类！
        // Deferred GPU resource release: when ReloadAsset destroys a texture/model
        // mid-frame, the old VkImage/VkBuffer may still be referenced by in-flight
        // command buffers. Keep the old asset alive for 3 frames (matching frames-in-flight).
        struct DeferredRelease {
            std::shared_ptr<void> Asset;
            int FramesRemaining = 3;
        };
        static std::vector<DeferredRelease> s_DeferredReleases;

        static std::unordered_map<UUID, std::shared_ptr<void>> s_Assets;
        static std::unordered_map<UUID, AssetMetadata> s_Registry;

        // 内置单例资产的句柄缓存
        static UUID s_BuiltInCubeHandle;
        static UUID s_BuiltInSphereHandle;
        static UUID s_BuiltInPlaneHandle;
        static UUID s_BuiltInMaterialHandle;

        // ==========================================
        // 异步加载控制系统
        // ==========================================
        static std::unordered_set<UUID> s_LoadingAssets;   // 防重入：记录正在后台加载的资产

        // Reverse dependency graph: asset UUID → assets that depend on it
        static std::unordered_map<UUID, std::unordered_set<UUID>> s_ReverseDeps;
        static std::mutex s_LoadingMutex;                   // 保护 s_LoadingAssets 的互斥锁
        static std::queue<std::function<void()>> s_MainThreadQueue; // 主线程 GPU 上传任务队列
        static std::mutex s_MainThreadQueueMutex;           // 保护任务队列的互斥锁
        static std::atomic<bool> s_BulkImportInProgress;    // 批量导入进行中标志（抑制 AssetWatcher）
    };

}
