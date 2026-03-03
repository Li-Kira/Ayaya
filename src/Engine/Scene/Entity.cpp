#include "ayapch.h"
#include "Entity.hpp"

namespace Ayaya {

    Entity::Entity(entt::entity handle, Scene* scene)
        : m_EntityHandle(handle), m_Scene(scene) {
    }

}