#include "ayapch.h"
#include "Scene.hpp"
#include "Entity.hpp"
#include "Components.hpp"

namespace Ayaya {

    Scene::Scene() {}

    Scene::~Scene() {}

    // 原来的 CreateEntity 现在直接调用带有随机 UUID 的版本
    Entity Scene::CreateEntity(const std::string& name) {
        return CreateEntityWithUUID(UUID(), name);
    }

    // 将原来 CreateEntity 里的逻辑全部移到这里
    Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name) {
        Entity entity = { m_Registry.create(), this };
        
        // --- 核心：给实体挂载唯一的 ID 组件 ---
        entity.AddComponent<IDComponent>(uuid);
        
        entity.AddComponent<TransformComponent>();
        
        // 处理重名逻辑...
        std::string baseName = name.empty() ? "Entity" : name;
        std::string uniqueName = baseName;
        int counter = 1;
        while (true) {
            bool nameExists = false;
            auto view = m_Registry.view<TagComponent>();
            for (auto e : view) {
                if (view.get<TagComponent>(e).Tag == uniqueName) {
                    nameExists = true;
                    break;
                }
            }
            if (!nameExists) break; 
            uniqueName = baseName + " (" + std::to_string(counter) + ")";
            counter++;
        }

        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = uniqueName;
        
        entity.AddComponent<RelationshipComponent>();
        m_RootEntities.push_back(entity.m_EntityHandle);

        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        auto& rel = entity.GetComponent<RelationshipComponent>();

        std::vector<entt::entity> childrenCopy = rel.Children;
        for (auto childID : childrenCopy) {
            DestroyEntity({ childID, this });
        }

        entity.SetParent({}); 

        auto it = std::find(m_RootEntities.begin(), m_RootEntities.end(), entity.m_EntityHandle);
        if (it != m_RootEntities.end()) m_RootEntities.erase(it);

        m_Registry.destroy(entity);
    }

    // ============================================================
    // 实现复制逻辑
    // ============================================================
    Entity Scene::DuplicateEntity(Entity entity) {
        std::string name = entity.GetComponent<TagComponent>().Tag;
        Entity newEntity = CreateEntity(name);

        newEntity.GetComponent<TransformComponent>() = entity.GetComponent<TransformComponent>();

        if (entity.HasComponent<SpriteRendererComponent>()) {
            newEntity.AddComponent<SpriteRendererComponent>(entity.GetComponent<SpriteRendererComponent>());
        }
        if (entity.HasComponent<CameraComponent>()) {
            auto& cameraComp = newEntity.AddComponent<CameraComponent>(entity.GetComponent<CameraComponent>());
            cameraComp.Primary = false; 
        }

        // =========================================================
        // 【核心修复】：必须预先拷贝被复制物体的父节点和子节点数组！
        // 因为后续的 DuplicateEntity 递归会调用 CreateEntity，
        // 可能会引发 EnTT 底层内存重分配，导致直接引用原组件失效闪退。
        // =========================================================
        entt::entity parentHandle = entity.GetComponent<RelationshipComponent>().Parent;
        std::vector<entt::entity> childrenCopy = entity.GetComponent<RelationshipComponent>().Children;
        
        if (parentHandle != entt::null) {
            Entity parent{ parentHandle, this };
            newEntity.SetParent(parent, false); 
        }

        for (auto childID : childrenCopy) {
            Entity child{ childID, this };
            Entity newChild = DuplicateEntity(child);
            newChild.SetParent(newEntity, false);
        }

        return newEntity;
    }
}