#pragma once

#include <entt/entt.hpp>
#include "Core/Timestep.hpp"
#include <vector>
#include <string>

namespace Ayaya {

    class Entity; // 前向声明，解决循环依赖

    class Scene {
    public:
        Scene();
        ~Scene();

        // 创建一个实体
        Entity CreateEntity(const std::string& name = std::string());

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