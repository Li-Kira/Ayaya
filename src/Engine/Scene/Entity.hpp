#pragma once

#include "Scene.hpp"
#include "Core/Log.hpp"
#include <entt/entt.hpp>
#include <algorithm>
#include "Components.hpp" 

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace Ayaya {

    class Entity {
    public:
        Entity() = default;
        
        // --- 修复 1：去掉大括号，仅仅声明，把实现留给 Entity.cpp ---
        Entity(entt::entity handle, Scene* scene);
        
        Entity(const Entity& other) = default;

        template<typename T, typename... Args> T& AddComponent(Args&&... args) { return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...); }
        template<typename T> T& GetComponent() const { return m_Scene->m_Registry.get<T>(m_EntityHandle); }
        template<typename T> bool HasComponent() const { return m_Scene->m_Registry.all_of<T>(m_EntityHandle); }
        template<typename T> void RemoveComponent() { m_Scene->m_Registry.remove<T>(m_EntityHandle); }
        
        // ==============================================
        // 新增：判断实体在层级树中是否真实可见
        // ==============================================
        bool IsActiveInHierarchy() const {
        // 1. 如果实体本身无效，直接返回 false
        if (!*this) return false;

        // 2. 检查自己的局部激活状态 (activeSelf)
        if (HasComponent<TagComponent>()) {
            if (!GetComponent<TagComponent>().IsActive) {
                return false; // 自己被隐藏了，直接判定为不可见
            }
        }

        // 3. 顺藤摸瓜，问问老父亲是不是被隐藏了
        if (HasComponent<RelationshipComponent>()) {
            auto& rel = GetComponent<RelationshipComponent>();
            if (rel.Parent != entt::null) {
                // 构建出父实体，并调用它自己的 IsActiveInHierarchy 进行递归
                Entity parent{ rel.Parent, m_Scene };
                return parent.IsActiveInHierarchy();
            }
        }

        // 4. 一路爬到根节点都没被隐藏，说明是真的可见！
        return true;
    }

        glm::mat4 GetWorldTransform() const {
            auto& transform = GetComponent<TransformComponent>();
            auto& rel = GetComponent<RelationshipComponent>();
            glm::mat4 localTransform = transform.GetTransform();
            if (rel.Parent != entt::null) {
                Entity parentEntity{ rel.Parent, m_Scene };
                return parentEntity.GetWorldTransform() * localTransform;
            }
            return localTransform;
        }

        bool IsDescendantOf(Entity potentialAncestor) const {
            if (!potentialAncestor) return false;
            auto& rel = GetComponent<RelationshipComponent>();
            if (rel.Parent == potentialAncestor.m_EntityHandle) return true;
            if (rel.Parent != entt::null) return Entity{ rel.Parent, m_Scene }.IsDescendantOf(potentialAncestor);
            return false;
        }

        void SetParent(Entity newParent, bool keepWorldTransform = true) {
            if (m_EntityHandle == newParent.m_EntityHandle || (newParent && newParent.IsDescendantOf(*this))) return; 

            auto& ourRel = GetComponent<RelationshipComponent>();
            auto& ourTransform = GetComponent<TransformComponent>();

            glm::mat4 oldWorldTransform = glm::mat4(1.0f);
            if (keepWorldTransform) oldWorldTransform = GetWorldTransform();

            // 1. 从旧父节点或根列表中移除
            if (ourRel.Parent != entt::null) {
                Entity oldParent{ ourRel.Parent, m_Scene };
                auto& oldChildren = oldParent.GetComponent<RelationshipComponent>().Children;
                auto it = std::find(oldChildren.begin(), oldChildren.end(), m_EntityHandle);
                if (it != oldChildren.end()) oldChildren.erase(it);
            } else {
                auto& roots = m_Scene->m_RootEntities;
                auto it = std::find(roots.begin(), roots.end(), m_EntityHandle);
                if (it != roots.end()) roots.erase(it);
            }

            ourRel.Parent = newParent ? (entt::entity)newParent : entt::null;

            // 2. 加入新父节点或根列表
            if (newParent) {
                newParent.GetComponent<RelationshipComponent>().Children.push_back(m_EntityHandle);
            } else {
                m_Scene->m_RootEntities.push_back(m_EntityHandle);
            }

            // 3. 补偿矩阵
            if (keepWorldTransform) {
                glm::mat4 newParentWorldTransform = newParent ? newParent.GetWorldTransform() : glm::mat4(1.0f);
                glm::mat4 newLocalTransform = glm::inverse(newParentWorldTransform) * oldWorldTransform;
                
                glm::vec3 scale, translation, skew;
                glm::quat rotation; glm::vec4 perspective;
                glm::decompose(newLocalTransform, scale, rotation, translation, skew, perspective);
                
                ourTransform.Translation = translation;
                ourTransform.Rotation = glm::eulerAngles(rotation);
                ourTransform.Scale = scale;
            }
        }

        // ==============================================
        // 核心排序方法：移动到目标物体的前面/后面
        // ==============================================
        void MoveTo(Entity target, bool before) {
            if (m_EntityHandle == target.m_EntityHandle) return;

            auto& ourRel = GetComponent<RelationshipComponent>();
            auto& targetRel = target.GetComponent<RelationshipComponent>();
            
            // 必须在同一个父节点下才可以排序
            if (ourRel.Parent != targetRel.Parent) return;

            std::vector<entt::entity>* list = nullptr;
            if (ourRel.Parent != entt::null) {
                Entity parent{ ourRel.Parent, m_Scene };
                list = &parent.GetComponent<RelationshipComponent>().Children;
            } else {
                list = &m_Scene->m_RootEntities;
            }

            auto it = std::find(list->begin(), list->end(), m_EntityHandle);
            if (it != list->end()) list->erase(it);

            auto targetIt = std::find(list->begin(), list->end(), target.m_EntityHandle);
            if (targetIt != list->end()) {
                if (before) list->insert(targetIt, m_EntityHandle);
                else list->insert(targetIt + 1, m_EntityHandle);
            } else {
                list->push_back(m_EntityHandle);
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

        // --- 修复 2：允许 Scene 访问 Entity 的私有成员 m_EntityHandle ---
        friend class Scene; 
    };
}