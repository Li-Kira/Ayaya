#include "ayapch.h"
#include "Scene.hpp"
#include "Entity.hpp"
#include "Components.hpp"

namespace Ayaya {

    Scene::Scene() {}

    Scene::~Scene() {}

    Entity Scene::CreateEntity(const std::string& name) {
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<TransformComponent>();
        
        // ==============================================
        // 核心升级：自动检测重名并添加序号 (如 Cube (1))
        // ==============================================
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
            if (!nameExists) break; // 名字不重复，跳出循环
            uniqueName = baseName + " (" + std::to_string(counter) + ")";
            counter++;
        }

        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = uniqueName;
        
        entity.AddComponent<RelationshipComponent>();

        // 所有刚创建的实体默认都是根节点
        m_RootEntities.push_back(entity.m_EntityHandle);

        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        auto& rel = entity.GetComponent<RelationshipComponent>();

        // 递归销毁子节点
        std::vector<entt::entity> childrenCopy = rel.Children;
        for (auto childID : childrenCopy) {
            DestroyEntity({ childID, this });
        }

        entity.SetParent({}); // 解绑父子关系

        // ==============================================
        // 核心维护：从根节点列表中彻底移除
        // ==============================================
        auto it = std::find(m_RootEntities.begin(), m_RootEntities.end(), entity.m_EntityHandle);
        if (it != m_RootEntities.end()) m_RootEntities.erase(it);

        m_Registry.destroy(entity);
    }
}