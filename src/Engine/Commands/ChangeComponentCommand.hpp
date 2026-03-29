#pragma once
#include "Engine/Core/Command.hpp"
#include "Engine/Scene/Entity.hpp"

namespace Ayaya {

    template<typename T>
    class ChangeComponentCommand : public Command {
    public:
        ChangeComponentCommand(Entity entity, const T& oldValue, const T& newValue)
            : m_Entity(entity), m_OldValue(oldValue), m_NewValue(newValue) {}

        virtual void Execute() override {
            if (m_Entity.HasComponent<T>()) {
                m_Entity.GetComponent<T>() = m_NewValue;
            }
        }

        virtual void Undo() override {
            if (m_Entity.HasComponent<T>()) {
                m_Entity.GetComponent<T>() = m_OldValue;
            }
        }

        virtual std::string GetName() const override {
            return "Change Component Value";
        }

    private:
        Entity m_Entity;
        T m_OldValue;
        T m_NewValue;
    };

}