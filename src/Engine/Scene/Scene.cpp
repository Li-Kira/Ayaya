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

}