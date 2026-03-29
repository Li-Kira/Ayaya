#pragma once
#include "Command.hpp"
#include <vector>

namespace Ayaya {

    class CommandHistory {
    public:
        CommandHistory() = default;

        void AddCommand(std::shared_ptr<Command> command) {
            // 执行命令
            command->Execute();

            // 如果我们在撤回了一些操作后，又执行了新操作，必须截断未来的历史！
            if (m_CommandIndex < (int)m_Commands.size() - 1) {
                m_Commands.erase(m_Commands.begin() + m_CommandIndex + 1, m_Commands.end());
            }

            // 合并连续操作（可选，用于优化拖拽）
            if (!m_Commands.empty() && m_Commands.back()->MergeWith(command.get())) {
                return; 
            }

            m_Commands.push_back(command);
            m_CommandIndex++;

            // 限制最大步数，防止内存爆炸
            if (m_Commands.size() > 100) {
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
    };

}