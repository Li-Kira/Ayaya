#pragma once
#include "Command.hpp"
#include <vector>

namespace Ayaya {

    class CommandHistory {
    public:
        CommandHistory() = default;

        // 【新增】：动态设置最大撤回步数，并自动裁剪超出部分
        void SetCapacity(size_t capacity) {
            m_Capacity = std::max((size_t)1, capacity);
            while (m_Commands.size() > m_Capacity) {
                m_Commands.erase(m_Commands.begin());
                m_CommandIndex--;
            }
            if (m_CommandIndex < -1) m_CommandIndex = -1;
        }

        void AddCommand(std::shared_ptr<Command> command) {
            command->Execute();

            if (m_CommandIndex < (int)m_Commands.size() - 1) {
                m_Commands.erase(m_Commands.begin() + m_CommandIndex + 1, m_Commands.end());
            }

            if (!m_Commands.empty() && m_Commands.back()->MergeWith(command.get())) {
                return; 
            }

            m_Commands.push_back(command);
            m_CommandIndex++;

            // 【修改】：使用动态设定的容量 m_Capacity，而不是硬编码的 100
            if (m_Commands.size() > m_Capacity) {
                m_Commands.erase(m_Commands.begin());
                m_CommandIndex--;
            }
        }

        void Undo() {
            if (m_CommandIndex >= 0) {
                m_Commands[m_CommandIndex]->Undo();
                m_CommandIndex--;
            }
        }

        void Redo() {
            if (m_CommandIndex < (int)m_Commands.size() - 1) {
                m_CommandIndex++;
                m_Commands[m_CommandIndex]->Execute();
            }
        }

        void Clear() {
            m_Commands.clear();
            m_CommandIndex = -1;
        }

        bool CanUndo() const { return m_CommandIndex >= 0; }
        bool CanRedo() const { return m_CommandIndex < (int)m_Commands.size() - 1; }

        const std::vector<std::shared_ptr<Command>>& GetCommands() const { return m_Commands; }
        int GetCommandIndex() const { return m_CommandIndex; }

    private:
        std::vector<std::shared_ptr<Command>> m_Commands;
        int m_CommandIndex = -1;
        size_t m_Capacity = 100; // 【新增】：默认容量 100
    };

}