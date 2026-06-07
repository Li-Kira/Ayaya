#pragma once
#include "Engine/Asset/Asset.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Scene.hpp"
#include <memory>

namespace Ayaya {

    class Prefab : public Asset {
    public:
        Prefab();
        ~Prefab() override = default;

        static AssetType GetStaticType() { return AssetType::Prefab; }
        AssetType GetType() const override { return GetStaticType(); }

        bool Save(const std::string& filepath);
        bool Load(const std::string& filepath);

        Scene* GetScene() { return m_Scene.get(); }
        Entity GetRootEntity() const { return m_RootEntity; }
        void SetRootEntity(Entity e) { m_RootEntity = e; }

    private:
        std::shared_ptr<Scene> m_Scene;
        Entity m_RootEntity;
    };

}
