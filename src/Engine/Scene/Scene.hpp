#pragma once

#include <entt/entt.hpp>
#include "Core/Timestep.hpp"
#include <vector>
#include <string>

#include "Engine/Core/UUID.hpp"

namespace Ayaya {

    class Entity; // 前向声明，解决循环依赖

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

        entt::registry& Reg() { return m_Registry; }

        // 获取有序的根节点列表
        const std::vector<entt::entity>& GetRootEntities() const { return m_RootEntities; }

    private:
        entt::registry m_Registry; 
        std::vector<entt::entity> m_RootEntities; 

        friend class Entity; 
    };

}