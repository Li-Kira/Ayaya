#include "HistoryPanel.hpp"
#include "../EditorLayer.hpp" // 用于获取 EditorLayer 的单例
#include <imgui.h>

namespace Ayaya {

    void HistoryPanel::OnImGuiRender() {
        // 如果面板被关闭，直接跳过渲染，节约性能
        if (!IsOpen) return;

        ImGui::Begin("History", &IsOpen);

        // 从全局编辑器层拉取历史记录实例
        auto& commandHistory = EditorLayer::Get().GetCommandHistory();
        const auto& commands = commandHistory.GetCommands();
        int currentIndex = commandHistory.GetCommandIndex();

        if (commands.empty()) {
            ImGui::TextDisabled("No history yet.");
        } else {
            ImGui::BeginChild("HistoryRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);

            for (int i = 0; i < (int)commands.size(); ++i) {
                bool isCurrent = (i == currentIndex);
                bool isUndone = (i > currentIndex);

                if (isCurrent) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.17f, 0.45f, 0.85f, 1.0f)); 
                } else if (isUndone) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f)); 
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_Text]); 
                }

                std::string commandText = std::to_string(i + 1) + ". " + commands[i]->GetName();
                
                if (isCurrent) {
                    commandText = "-> " + commandText;
                } else {
                    commandText = "   " + commandText;
                }

                ImGui::TextUnformatted(commandText.c_str());
                ImGui::PopStyleColor();
            }
            
            // 自动滚动到最底部
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            
            ImGui::EndChild();
        }

        ImGui::Separator();
        
        if (ImGui::Button("Clear History", ImVec2(150, 0))) {
            commandHistory.Clear();
        }

        ImGui::End();
    }

}