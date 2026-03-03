#pragma once

#include "Scene.hpp"
#include "Core/Log.hpp"
#include <entt/entt.hpp>
#include <algorithm>
#include "Components.hpp" 

namespace Ayaya {

    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene);
        Entity(const Entity& other) = default;

        template<typename T, typename... Args> T& AddComponent(Args&&... args) { return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...); }
        template<typename T> T& GetComponent() { return m_Scene->m_Registry.get<T>(m_EntityHandle); }
        template<typename T> bool HasComponent() { return m_Scene->m_Registry.all_of<T>(m_EntityHandle); }
        template<typename T> void RemoveComponent() { m_Scene->m_Registry.remove<T>(m_EntityHandle); }

        // ==============================================
        // 核心升级：安全的重新认父逻辑 (Reparenting)
        // ==============================================
        void SetParent(Entity newParent) {
            if (m_EntityHandle == newParent.m_EntityHandle) return;

            auto& ourRel = GetComponent<RelationshipComponent>();

            // 1. 从旧父节点移除
            if (ourRel.Parent != entt::null) {
                Entity oldParent{ ourRel.Parent, m_Scene };
                auto& oldChildren = oldParent.GetComponent<RelationshipComponent>().Children;
                auto it = std::find(oldChildren.begin(), oldChildren.end(), m_EntityHandle);
                if (it != oldChildren.end()) {
                    oldChildren.erase(it); // 只有找到才移除
                }
            }

            // 2. 设置新父节点
            ourRel.Parent = newParent ? (entt::entity)newParent : entt::null;

            // 3. 添加入新父节点
            if (newParent) {
                newParent.GetComponent<RelationshipComponent>().Children.push_back(m_EntityHandle);
            }
        }

        operator bool() const { return m_EntityHandle != entt::null; }
        operator entt::entity() const { return m_EntityHandle; }
        operator uint32_t() const { return (uint32_t)m_EntityHandle; }

        bool operator==(const Entity& other) const { return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; }
        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        entt::entity m_EntityHandle{ entt::null };
        Scene* m_Scene = nullptr;
    };

}