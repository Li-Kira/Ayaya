#pragma once
#include "Command.hpp"
#include "Engine/Scene/Entity.hpp"
#include <vector>

namespace Ayaya {

    // ==========================================
    // 1. 批量命令 (宏命令)：用于多选时的统一撤回
    // ==========================================
    class MacroCommand : public Command {
    public:
        MacroCommand(const std::string& name) : m_Name(name) {}

        void AddCommand(std::shared_ptr<Command> command) {
            m_Commands.push_back(command);
        }

        virtual void Execute() override {
            for (auto& cmd : m_Commands) cmd->Execute();
        }

        virtual void Undo() override {
            // 撤回时必须反向执行！
            for (auto it = m_Commands.rbegin(); it != m_Commands.rend(); ++it) {
                (*it)->Undo();
            }
        }

        virtual std::string GetName() const override { return m_Name; }
        bool IsEmpty() const { return m_Commands.empty(); }

    private:
        std::string m_Name;
        std::vector<std::shared_ptr<Command>> m_Commands;
    };

    // ==========================================
    // 2. 万能组件修改命令 (利用 C++ 模板)
    // ==========================================
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

        virtual std::string GetName() const override { return "Change Component"; }

    private:
        Entity m_Entity;
        T m_OldValue;
        T m_NewValue;
    };

}