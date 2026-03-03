#pragma once

#include "Scene.hpp"
#include "Core/Log.hpp"
#include <entt/entt.hpp>

namespace Ayaya {

    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene);
        Entity(const Entity& other) = default;

        // --- 核心模板方法：通过 Scene 的 Registry 操作组件 ---

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args) {
            if (HasComponent<T>()) {
                AYAYA_CORE_WARN("Entity already has component!");
            }
            return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
        }

        template<typename T>
        T& GetComponent() {
            if (!HasComponent<T>()) {
                AYAYA_CORE_WARN("Entity does not have component!");
            }
            return m_Scene->m_Registry.get<T>(m_EntityHandle);
        }

        template<typename T>
        bool HasComponent() {
            return m_Scene->m_Registry.all_of<T>(m_EntityHandle);
        }

        template<typename T>
        void RemoveComponent() {
            if (!HasComponent<T>()) {
                AYAYA_CORE_WARN("Entity does not have component!");
            }
            m_Scene->m_Registry.remove<T>(m_EntityHandle);
        }

        // --- 运算符重载，让 Entity 更好用 ---
        
        // 允许 if(entity) 判断实体是否有效
        operator bool() const { return m_EntityHandle != entt::null; }
        // 允许隐式转换为 EnTT 的原生 entity 类型
        operator entt::entity() const { return m_EntityHandle; }
        operator uint32_t() const { return (uint32_t)m_EntityHandle; }

        bool operator==(const Entity& other) const { 
            return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; 
        }
        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        entt::entity m_EntityHandle{ entt::null };
        Scene* m_Scene = nullptr;
    };

}