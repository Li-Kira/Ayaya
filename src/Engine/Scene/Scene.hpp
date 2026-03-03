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

        void DestroyEntity(Entity entity);

        // 预留给未来的系统更新（比如渲染系统、物理系统）
        // void OnUpdate(Timestep ts);
        entt::registry& Reg() { return m_Registry; }

        // --- 新增：获取有序的根节点列表 ---
        const std::vector<entt::entity>& GetRootEntities() const { return m_RootEntities; }

    private:
        entt::registry m_Registry; // EnTT 的核心：存储所有实体和组件的数据库
        std::vector<entt::entity> m_RootEntities; // 维护根节点的渲染顺序

        friend class Entity; // 允许 Entity 类直接访问 m_Registry 来添加/获取组件
    };

}