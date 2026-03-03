#pragma once

#include <entt/entt.hpp>
#include "Core/Timestep.hpp"

namespace Ayaya {

    class Entity; // 前向声明，解决循环依赖

    class Scene {
    public:
        Scene();
        ~Scene();

        // 创建一个实体
        Entity CreateEntity(const std::string& name = std::string());

        // 预留给未来的系统更新（比如渲染系统、物理系统）
        // void OnUpdate(Timestep ts);
        entt::registry& Reg() { return m_Registry; }

    private:
        entt::registry m_Registry; // EnTT 的核心：存储所有实体和组件的数据库

        friend class Entity; // 允许 Entity 类直接访问 m_Registry 来添加/获取组件
    };

}