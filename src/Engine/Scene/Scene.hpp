#pragma once

#include <entt/entt.hpp>
#include "Core/Timestep.hpp"
#include <vector>
#include <string>

#include "Engine/Core/UUID.hpp"
#include "Engine/Animation/TweenManager.hpp"

class b2World;

namespace Ayaya {

    class Entity; // 前向声明，解决循环依赖
    class Model;         
    struct ModelNode;

    class Scene {
    public:
        Scene();
        ~Scene();

        // 创建一个实体 (自动分配新 UUID)
        Entity CreateEntity(const std::string& name = std::string());
        
        // --- 新增：使用指定的 UUID 创建实体 (用于反序列化读取文件时) ---
        Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());

        // 销毁一个实体
        void DestroyEntity(Entity entity);

        // ==============================================
        // 核心：复制一个实体及其所有的子节点和组件
        // ==============================================
        Entity DuplicateEntity(Entity entity);
        Entity InstantiatePrefab(class Prefab* prefab);

        void PropagateActiveState(Entity entity);

        void InvalidateAssetCache(UUID assetHandle);

        entt::registry& Reg() { return m_Registry; }

        // 获取有序的根节点列表
        const std::vector<entt::entity>& GetRootEntities() const { return m_RootEntities; }
        Entity InstantiateModel(const std::shared_ptr<Model>& model);
        Entity InstantiateModel(const std::shared_ptr<Model>& model, Entity parentEntity);
        Entity InstantiateModelNode(const ModelNode& node, Entity parentEntity);

        // ==========================================
        // 运行时的总开关 (生命周期)
        // ==========================================
        void OnRuntimeStart();
        void OnRuntimeStop();

        // ==========================================
        // 新增：物理系统的生命周期
        // ==========================================
        void OnPhysics2DStart();
        void OnPhysics2DStop();
        
        // 我们需要一个专供游戏运行时的 Update (不包含编辑器相机逻辑)
        void OnUpdateRuntime(Timestep ts);

        // 收集当前场景中所有实体引用的资产 UUID（用于 GC Mark 阶段）
        std::unordered_set<UUID> GetActiveAssetHandles() const;

        // O(1) UUID → Entity lookup (synced in Create/Destroy entity)
        Entity GetEntityByUUID(UUID uuid);

        TweenManager& GetTweenManager() { return m_TweenManager; }
        float GetAnimationTime() const { return m_AnimationTime; }
        void  SetAnimationTime(float t) { m_AnimationTime = t; }

    private:
        entt::registry m_Registry;
        std::vector<entt::entity> m_RootEntities; 

        friend class Entity; 

        b2World* m_PhysicsWorld = nullptr;
        TweenManager m_TweenManager;
        float m_AnimationTime = 0.0f;
        std::unordered_map<UUID, entt::entity> m_EntityMap;
    };

}