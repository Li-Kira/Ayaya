#include "ayapch.h"
#include "FrameDebuggerPanel.hpp"
#include <imgui.h>

namespace Ayaya {

    void FrameDebuggerPanel::OnImGuiRender() {
        if (!IsOpen) return;

        ImGui::Begin("Frame Debugger", &IsOpen);

        if (!m_Renderer) {
            ImGui::TextDisabled("No Renderer Context Attached.");
            ImGui::End();
            return;
        }

        // ==========================================
        // 1. 定义我们想要查看的所有管线节点
        // ==========================================
        const char* viewNames[] = {
            "Final Output (FXAA)",
            "Shadow Map",
            "GBuffer: Position",
            "GBuffer: Normal",
            "GBuffer: Albedo",
            "GBuffer: PBR (Metal/Rough/AO)",
            "Lighting Pass Output",
            "Bloom Pass Output",
            "Post Process Output"
        };

        // 这里的 Key 必须与各个 Pass 写入黑板时使用的完全一致！
        const char* blackboardKeys[] = {
            "Final_Output",
            "ShadowMap",
            "GBuffer_Position",
            "GBuffer_Normal",
            "GBuffer_Albedo",
            "GBuffer_PBR",
            "Lighting_Output",
            "Bloom_Output",
            "PostProcess_Output"
        };

        // ==========================================
        // 2. 渲染控制栏
        // ==========================================
        ImGui::PushItemWidth(-1.0f); // 让下拉框填满宽度
        ImGui::Combo("##RenderTargets", &m_CurrentViewIndex, viewNames, IM_ARRAYSIZE(viewNames));
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ==========================================
        // 3. 从黑板提取贴图并等比渲染
        // ==========================================
        uint32_t textureID = m_Renderer->GetBlackboardTextureID(blackboardKeys[m_CurrentViewIndex]);

        if (textureID != 0) {
            ImVec2 availSize = ImGui::GetContentRegionAvail();
            
            // 简单的等比缩放逻辑 (假设标准宽高比为 16:9)
            float aspect = 16.0f / 9.0f;
            ImVec2 imageSize;
            if (availSize.x / availSize.y > aspect) {
                imageSize.y = availSize.y;
                imageSize.x = imageSize.y * aspect;
            } else {
                imageSize.x = availSize.x;
                imageSize.y = imageSize.x / aspect;
            }

            // 居中显示图片
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availSize.x - imageSize.x) * 0.5f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (availSize.y - imageSize.y) * 0.5f);

            // 注意：OpenGL 贴图需要翻转 UV 才能在 ImGui 中正向显示
            ImGui::Image((ImTextureID)(intptr_t)textureID, imageSize, ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImVec2 availSize = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + availSize.y * 0.5f - 10.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availSize.x * 0.5f - 100.0f);
            ImGui::TextDisabled("Texture not generated in this frame.");
        }

        ImGui::End();
    }

}