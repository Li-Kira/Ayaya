#include "ayapch.h"
#include "Prefab.hpp"
#include "Engine/Scene/SceneSerializer.hpp"
#include "Engine/Scene/Components.hpp"
#include <fstream>

namespace Ayaya {

    Prefab::Prefab() {
        m_Scene = std::make_shared<Scene>();
    }

    bool Prefab::Save(const std::string& filepath) {
        if (!m_Scene || !m_RootEntity) return false;

        EditorState dummy;
        SceneSerializer serializer(m_Scene);
        serializer.Serialize(filepath, dummy);
        return true;
    }

    bool Prefab::Load(const std::string& filepath) {
        if (!m_Scene) m_Scene = std::make_shared<Scene>();

        EditorState dummy;
        SceneSerializer serializer(m_Scene);
        if (!serializer.Deserialize(filepath, dummy)) return false;

        // Find first root entity (has no parent or parent is null)
        auto view = m_Scene->Reg().view<RelationshipComponent>();
        for (auto entityID : view) {
            auto& rel = view.get<RelationshipComponent>(entityID);
            if (rel.Parent == entt::null) {
                m_RootEntity = Entity{ entityID, m_Scene.get() };
                break;
            }
        }

        return m_RootEntity ? true : false;
    }

}
