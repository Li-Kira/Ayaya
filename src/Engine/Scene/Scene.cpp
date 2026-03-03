#include "ayapch.h"
#include "Scene.hpp"
#include "Entity.hpp"
#include "Components.hpp"

namespace Ayaya {

    Scene::Scene() {}

    Scene::~Scene() {}

    Entity Scene::CreateEntity(const std::string& name) {
        // 在 EnTT 中创建一个实体 ID
        Entity entity = { m_Registry.create(), this };
        
        // 任何被创建的实体，默认都自带 Transform（变换）组件
        entity.AddComponent<TransformComponent>();
        
        // 给实体打上名字标签，如果不传名字，就默认叫 "Entity"
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = name.empty() ? "Entity" : name;

        return entity;
    }

}