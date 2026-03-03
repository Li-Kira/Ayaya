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
        
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = name.empty() ? "Entity" : name;
        
        // ==============================================
        // 新增：默认挂载关系组件，让它能参与层级树
        // ==============================================
        entity.AddComponent<RelationshipComponent>();

        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        auto& rel = entity.GetComponent<RelationshipComponent>();

        // 1. 递归销毁所有子节点 (级联删除)
        // 注意：必须拷贝一份 Children 数组，因为 DestroyEntity 会修改原数组
        std::vector<entt::entity> childrenCopy = rel.Children;
        for (auto childID : childrenCopy) {
            DestroyEntity({ childID, this });
        }

        // 2. 从当前父亲的列表中解绑自己
        entity.SetParent({}); // 传入空实体，自动执行脱离逻辑

        // 3. 彻底从 EnTT 注册表中抹除
        m_Registry.destroy(entity);
    }
}