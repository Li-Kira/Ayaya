#pragma once

#include "Scene.hpp"
#include "Core/Log.hpp"
#include <entt/entt.hpp>
#include <algorithm>
#include "Components.hpp" 

// 引入矩阵分解功能
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace Ayaya {

    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene);
        Entity(const Entity& other) = default;

        template<typename T, typename... Args> T& AddComponent(Args&&... args) { return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...); }
        template<typename T> T& GetComponent() const { return m_Scene->m_Registry.get<T>(m_EntityHandle); }
        template<typename T> bool HasComponent() const { return m_Scene->m_Registry.all_of<T>(m_EntityHandle); }
        template<typename T> void RemoveComponent() { m_Scene->m_Registry.remove<T>(m_EntityHandle); }

        // ==============================================
        // 核心 1：递归获取绝对世界矩阵
        // ==============================================
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

        // ==============================================
        // 核心 2：防死循环检测 (检查目标是不是自己的子孙)
        // ==============================================
        bool IsDescendantOf(Entity potentialAncestor) const {
            if (!potentialAncestor) return false;
            auto& rel = GetComponent<RelationshipComponent>();
            if (rel.Parent == potentialAncestor.m_EntityHandle) return true;
            if (rel.Parent != entt::null) {
                Entity parentEntity{ rel.Parent, m_Scene };
                return parentEntity.IsDescendantOf(potentialAncestor);
            }
            return false;
        }

        // ==============================================
        // 核心 3：Unity 级别的 SetParent (World Position Stays)
        // ==============================================
        void SetParent(Entity newParent, bool keepWorldTransform = true) {
            // 防止认自己为父，或认自己的子孙为父（会引发死循环崩溃）
            if (m_EntityHandle == newParent.m_EntityHandle || (newParent && newParent.IsDescendantOf(*this))) {
                return; 
            }

            auto& ourRel = GetComponent<RelationshipComponent>();
            auto& ourTransform = GetComponent<TransformComponent>();

            // 1. 记录修改前的世界矩阵
            glm::mat4 oldWorldTransform = glm::mat4(1.0f);
            if (keepWorldTransform) {
                oldWorldTransform = GetWorldTransform();
            }

            // 2. 从旧父节点移除
            if (ourRel.Parent != entt::null) {
                Entity oldParent{ ourRel.Parent, m_Scene };
                auto& oldChildren = oldParent.GetComponent<RelationshipComponent>().Children;
                auto it = std::find(oldChildren.begin(), oldChildren.end(), m_EntityHandle);
                if (it != oldChildren.end()) {
                    oldChildren.erase(it);
                }
            }

            // 3. 设置新父节点
            ourRel.Parent = newParent ? (entt::entity)newParent : entt::null;

            // 4. 添加入新父节点
            if (newParent) {
                newParent.GetComponent<RelationshipComponent>().Children.push_back(m_EntityHandle);
            }

            // 5. 重新计算 Local Transform，抵消父节点变化带来的空间跳跃
            if (keepWorldTransform) {
                glm::mat4 newParentWorldTransform = glm::mat4(1.0f);
                if (newParent) {
                    newParentWorldTransform = newParent.GetWorldTransform();
                }
                
                // 新局部矩阵 = 新父节点世界矩阵的逆 * 原世界矩阵
                glm::mat4 newLocalTransform = glm::inverse(newParentWorldTransform) * oldWorldTransform;
                
                // 分解矩阵，回填到组件
                glm::vec3 scale;
                glm::quat rotation;
                glm::vec3 translation;
                glm::vec3 skew;
                glm::vec4 perspective;
                glm::decompose(newLocalTransform, scale, rotation, translation, skew, perspective);
                
                ourTransform.Translation = translation;
                ourTransform.Rotation = glm::eulerAngles(rotation);
                ourTransform.Scale = scale;
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