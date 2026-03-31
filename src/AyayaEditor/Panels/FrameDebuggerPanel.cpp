#include "ayapch.h"
#include "FrameDebuggerPanel.hpp"
#include <imgui.h>

namespace Ayaya {

    void FrameDebuggerPanel::OnImGuiRender() {
        if (!IsOpen) return;

        ImGui::SetNextWindowSize(ImVec2(1050, 750), ImGuiCond_FirstUseEver);
        ImGui::Begin("Frame Debugger", &IsOpen);

        if (!m_Renderer) {
            ImGui::TextDisabled("No Renderer Attached.");
            ImGui::End();
            return;
        }

        auto& liveContext = m_Renderer->GetRenderContext();

        // ==========================================
        // 工业级截帧快照 (Frame Capture)
        // ==========================================
        static bool s_IsCaptured = false;
        static std::vector<DrawCallStep> s_CapturedSteps;
        static std::unordered_map<std::string, PassProfileData> s_CapturedProfiles;
        
        ImGui::PushStyleColor(ImGuiCol_Button, s_IsCaptured ? ImVec4(0.8f, 0.2f, 0.2f, 1.0f) : ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, s_IsCaptured ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
        if (ImGui::Button(s_IsCaptured ? "Release Frame (Live)" : "Capture Frame (Freeze)", ImVec2(220, 30))) {
            s_IsCaptured = !s_IsCaptured;
            if (s_IsCaptured) {
                s_CapturedSteps = liveContext.FrameSteps;
                s_CapturedProfiles = liveContext.PassProfiles;
            } else {
                m_Renderer->SetDebugStepLimit(-1);
            }
        }
        ImGui::PopStyleColor(2);
        
        ImGui::SameLine();
        if (s_IsCaptured) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Frame is FROZEN. You can safely inspect Draw Calls without losing performance data.");
        } else {
            ImGui::TextDisabled("Live mode. Data is updating every frame.");
        }
        
        ImGui::Spacing();
        ImGui::Separator();

        const auto& displaySteps = s_IsCaptured ? s_CapturedSteps : liveContext.FrameSteps;
        const auto& displayProfiles = s_IsCaptured ? s_CapturedProfiles : liveContext.PassProfiles;

        if (ImGui::BeginTabBar("FrameDebuggerTabs", ImGuiTabBarFlags_None)) {
            
            // ------------------------------------------
            // TAB 1: Draw Call 回放拦截器 (集成了动态下拉列表)
            // ------------------------------------------
            if (ImGui::BeginTabItem("Draw Call Inspector")) {
                static int s_CurrentStep = -1;

                ImGui::Spacing();
                
                if (s_IsCaptured) {
                    if (s_CurrentStep < 0) s_CurrentStep = 0;
                    if (s_CurrentStep >= displaySteps.size() && !displaySteps.empty()) s_CurrentStep = displaySteps.size() - 1;
                    
                    ImGui::SliderInt("Playback Step", &s_CurrentStep, 0, std::max(0, (int)displaySteps.size() - 1));
                    m_Renderer->SetDebugStepLimit(s_CurrentStep);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please click 'Capture Frame' above to enable step-by-step playback.");
                    s_CurrentStep = -1; 
                }

                ImGui::Separator();

                ImGui::Columns(2, "FrameDebuggerCols", true);
                ImGui::SetColumnWidth(0, 320.0f);

                // --- 左侧：树形结构列表 ---
                ImGui::BeginChild("DrawCallList");
                std::string currentPass = "";
                bool passNodeOpen = false;

                for (int i = 0; i < displaySteps.size(); i++) {
                    if (displaySteps[i].PassName != currentPass) {
                        if (passNodeOpen) ImGui::TreePop();
                        currentPass = displaySteps[i].PassName;
                        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
                        passNodeOpen = ImGui::TreeNodeEx(currentPass.c_str());
                        ImGui::PopStyleColor();
                    }

                    if (passNodeOpen) {
                        std::string label = "  " + std::to_string(i) + ": Draw " + displaySteps[i].TargetName;
                        if (ImGui::Selectable(label.c_str(), s_CurrentStep == i)) {
                            if (!s_IsCaptured) {
                                s_IsCaptured = true;
                                s_CapturedSteps = liveContext.FrameSteps;
                                s_CapturedProfiles = liveContext.PassProfiles;
                            }
                            s_CurrentStep = i;
                        }
                    }
                }
                if (passNodeOpen) ImGui::TreePop();
                ImGui::EndChild();

                ImGui::NextColumn();

                // --- 右侧：视口回放与动态下拉列表 ---
                ImGui::BeginChild("DrawCallDetails");
                if (s_IsCaptured && s_CurrentStep >= 0 && s_CurrentStep < displaySteps.size()) {
                    auto& step = displaySteps[s_CurrentStep];
                    ImGui::Text("Shader Program: %s", step.ShaderName.c_str());
                    ImGui::Text("Triangles Drawn: %d", step.TriangleCount);
                    ImGui::Separator();
                    ImGui::Spacing();

                    // ==========================================
                    // 【核心升级】：上下文敏感的动态下拉列表
                    // ==========================================
                    std::vector<const char*> availableTargets;
                    std::vector<std::string> targetKeys;

                    // 根据当前的 Pass 动态生成可查看的贴图列表
                    if (step.PassName == "Shadow Map Pass" || step.PassName == "Shadow Pass") {
                        availableTargets = { "Shadow Depth Map" };
                        targetKeys = { "ShadowMap" };
                    }
                    else if (step.PassName == "G-Buffer Geometry Pass" || step.PassName == "G-Buffer Pass") {
                        availableTargets = { "Albedo (RGB)", "World Normal (RGB)", "World Position (RGB)", "PBR Data (M/R/AO)" };
                        targetKeys = { "GBuffer_Albedo", "GBuffer_Normal", "GBuffer_Position", "GBuffer_PBR" };
                    }
                    else if (step.PassName == "Lighting & Forward Pass" || step.PassName == "Lighting Pass" || step.PassName == "Selection Pass") {
                        availableTargets = { "Lighting Result" };
                        targetKeys = { "Lighting_Output" };
                    }
                    else if (step.PassName == "Bloom Pass") {
                        availableTargets = { "Bloom Highlight & Blur" };
                        targetKeys = { "Bloom_Output" };
                    }
                    else if (step.PassName == "Post Process Pass") {
                        availableTargets = { "Tone Mapping Output" };
                        targetKeys = { "PostProcess_Output" };
                    }
                    else if (step.PassName == "FXAA Pass") {
                        availableTargets = { "Final Anti-Aliased Output" };
                        targetKeys = { "Final_Output" };
                    }
                    else {
                        availableTargets = { "Final Output" };
                        targetKeys = { "Final_Output" };
                    }

                    static int s_SelectedTargetIndex = 0;
                    // 防止切换 Pass 时索引越界
                    if (s_SelectedTargetIndex >= availableTargets.size()) {
                        s_SelectedTargetIndex = 0;
                    }

                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("View Target:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(250.0f);
                    ImGui::Combo("##ViewTargetCombo", &s_SelectedTargetIndex, availableTargets.data(), availableTargets.size());

                    std::string textureKey = targetKeys[s_SelectedTargetIndex];

                    // 从黑板提取 FBO 贴图
                    uint32_t texID = m_Renderer->GetBlackboardTextureID(textureKey);
                    if (texID != 0) {
                        ImVec2 availSize = ImGui::GetContentRegionAvail();
                        float aspect = 16.0f / 9.0f;
                        ImVec2 imageSize;
                        if (availSize.x / availSize.y > aspect) {
                            imageSize.y = availSize.y;  imageSize.x = imageSize.y * aspect;
                        } else {
                            imageSize.x = availSize.x;  imageSize.y = imageSize.x / aspect;
                        }
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availSize.x - imageSize.x) * 0.5f);
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (availSize.y - imageSize.y) * 0.5f);
                        ImGui::Image((void*)(intptr_t)texID, imageSize, ImVec2(0, 1), ImVec2(1, 0));
                    } else {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Render Target not available or cleared.");
                    }
                } else {
                    ImGui::TextDisabled("Select a draw call on the left to inspect.");
                }
                ImGui::EndChild();
                ImGui::Columns(1);
                ImGui::EndTabItem();
            }

            // ------------------------------------------
            // TAB 2: 管线性能剖析 (Pipeline Profiler)
            // ------------------------------------------
            if (ImGui::BeginTabItem("Pipeline Profiler")) {
                ImGui::Spacing();
                
                if (ImGui::BeginTable("ProfilerTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                    ImGui::TableSetupColumn("Pass Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("CPU Time (ms)", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    ImGui::TableSetupColumn("GPU Time (ms)", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    ImGui::TableSetupColumn("Draw Calls", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableHeadersRow();

                    float totalCPU = 0.0f, totalGPU = 0.0f;
                    uint32_t totalDC = 0, totalTris = 0;

                    for (const auto& [name, profile] : displayProfiles) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::Text("%s", name.c_str());
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", profile.CPUTime);
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", profile.GPUTime);
                        ImGui::TableSetColumnIndex(3); ImGui::Text("%u", profile.DrawCalls);
                        ImGui::TableSetColumnIndex(4); ImGui::Text("%u", profile.Triangles);

                        totalCPU += profile.CPUTime;
                        totalGPU += profile.GPUTime;
                        totalDC += profile.DrawCalls;
                        totalTris += profile.Triangles;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); 
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
                    ImGui::Text("TOTAL FRAME COST");
                    ImGui::PopFont();
                    ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.3f", totalCPU);
                    ImGui::TableSetColumnIndex(2); ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "%.3f", totalGPU);
                    ImGui::TableSetColumnIndex(3); ImGui::TextColored(ImVec4(0, 1, 0, 1), "%u", totalDC);
                    ImGui::TableSetColumnIndex(4); ImGui::TextColored(ImVec4(0, 1, 0, 1), "%u", totalTris);

                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            // ------------------------------------------
            // TAB 3: 渲染目标观察室 (Memory)
            // ------------------------------------------
            if (ImGui::BeginTabItem("Render Targets Memory")) {
                ImGui::Spacing();
                ImGui::TextDisabled("All active Framebuffer Objects currently managed by the Render Graph.");
                ImGui::Spacing();

                int id = 0;
                for (const auto& [name, fbo] : liveContext.Framebuffers) {
                    ImGui::PushID(id++);
                    if (ImGui::CollapsingHeader(std::string(name).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Indent();
                        ImGui::Text("Resolution: %d x %d", fbo->GetSpecification().Width, fbo->GetSpecification().Height);
                        ImGui::Text("Samples: %d", fbo->GetSpecification().Samples);
                        
                        ImGui::Spacing();
                        uint32_t texID = fbo->GetColorAttachmentRendererID(0); 
                        ImGui::Image((void*)(intptr_t)texID, ImVec2(320.0f, 180.0f), ImVec2(0, 1), ImVec2(1, 0));
                        ImGui::Unindent();
                        ImGui::Spacing();
                    }
                    ImGui::PopID();
                }
                if (liveContext.Framebuffers.empty()) ImGui::TextDisabled("No Framebuffers registered.");
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}