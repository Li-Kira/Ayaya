#include "ayapch.h"
#include "ExportPanel.hpp"
#include "UI_Widgets.hpp"
#include "Project/ExportPackager.hpp"
#include "Utils/PlatformUtils.hpp"

namespace Ayaya {

    void ExportPanel::OnImGuiRender(const std::shared_ptr<Scene>& activeScene) {
        if (m_ShowPopup) {
            ImGui::OpenPopup("Export Game");
            m_ShowPopup = false;
        }

        UI::PushPopupStyles(360.0f);
        if (ImGui::BeginPopupModal("Export Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            UI::DrawMenuHeader(ICON_FA_FILE_EXPORT " Export Game");

            // ── 设置：两栏属性表 (对齐 Properties 面板) ──
            if (UI::BeginPropertyTable("##ExportSettings", 110.0f, 1.6f)) {
                UI::DrawPropertyLabel("Game Name");
                ImGui::InputText("##GameName", m_GameName, sizeof(m_GameName));

                UI::DrawPropertyLabel("Platform");
                const char* platforms[] = { "macOS", "Windows" };
                ImGui::Combo("##Platform", &m_Platform, platforms, 2);

                UI::DrawPropertyLabel("Graphics API");
                const char* apis[] = { "Vulkan", "OpenGL" };
                ImGui::Combo("##GraphicsAPI", &m_GraphicsAPI, apis, 2);

                UI::DrawPropertyLabel("Width");
                ImGui::InputInt("##Width", &m_Width);
                if (m_Width < 1) m_Width = 1;

                UI::DrawPropertyLabel("Height");
                ImGui::InputInt("##Height", &m_Height);
                if (m_Height < 1) m_Height = 1;

                UI::DrawPropertyLabel("VSync");
                ImGui::Checkbox("##VSync", &m_VSync);

                UI::DrawPropertyLabel("Destination");
                {
                    std::string destDisplay = m_DestDir;
                    if (destDisplay.size() > 40)
                        destDisplay = "..." + destDisplay.substr(destDisplay.size() - 40);
                    ImGui::TextUnformatted(destDisplay.empty() ? "(none)" : destDisplay.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FOLDER_OPEN "##Browse")) {
                    std::string dir = FileDialogs::OpenFolder();
                    if (!dir.empty()) m_DestDir = dir;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose destination folder");

                ImGui::EndTable();
            }

            ImGui::Spacing();

            // ── 导出进度 / 操作按钮 ──
            if (m_Exporting.load()) {
                if (m_Done.load()) {
                    // 后台线程结束 → 收尾
                    if (m_Worker.joinable()) m_Worker.join();
                    m_Exporting = false;
                    m_Result = m_Success.load() ? "Export succeeded!" : "Export failed. See console log.";
                } else {
                    float p = m_Progress.load();
                    char overlay[16];
                    snprintf(overlay, sizeof(overlay), "%.0f%%", p * 100.0f);
                    ImGui::ProgressBar(p, ImVec2(360.0f, 0.0f), overlay);  // 固定宽度，避免撑宽窗口
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    ImGui::TextUnformatted(m_ProgressMsg.c_str());
                }
            } else {
                bool canExport = !m_DestDir.empty();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.90f, 1.0f));
                if (ImGui::Button(ICON_FA_ROCKET " Export", ImVec2(140.0f, 0.0f)) && canExport) {
                    StartExport(activeScene);
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_TIMES " Close", ImVec2(90.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                }

                if (!m_Result.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        m_Success.load() ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
                                         : ImVec4(0.90f, 0.35f, 0.35f, 1.0f));
                    ImGui::TextWrapped("%s", m_Result.c_str());
                    ImGui::PopStyleColor();
                }
            }

            ImGui::EndPopup();
        }
        UI::PopPopupStyles();
    }

    void ExportPanel::StartExport(const std::shared_ptr<Scene>& activeScene) {
        ExportSettings s;
        s.GameName     = m_GameName;
        s.Platform     = (m_Platform == 1) ? "Windows" : "macOS";
        s.GraphicsAPI  = (m_GraphicsAPI == 1) ? "OpenGL" : "Vulkan";
        s.WindowWidth  = (uint32_t)m_Width;
        s.WindowHeight = (uint32_t)m_Height;
        s.VSync        = m_VSync;
        s.DestDir      = m_DestDir;

        m_Progress = 0.0f;
        m_Done     = false;
        m_Success  = false;
        m_Result.clear();
        { std::lock_guard<std::mutex> lock(m_Mutex); m_ProgressMsg.clear(); }

        // 阶段 1：主线程同步收集 (读 Project/VFS/AssetManager/Scene，快)；失败直接报错
        ExportPackager::ExportPlan plan;
        if (!ExportPackager::Collect(s, activeScene, plan)) {
            m_Exporting = false;
            m_Result = "Export failed. See console log.";
            return;
        }

        if (m_Worker.joinable()) m_Worker.join();  // 兜底：上一次线程若未回收则回收

        m_Exporting = true;
        // 阶段 2：后台线程执行 (只碰 value 类型 plan + 文件系统 + 子进程)
        m_Worker = std::thread([this, s, plan]() {
            bool ok = ExportPackager::Execute(s, plan,
                [this](float p, const std::string& msg) {
                    m_Progress.store(p);
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    m_ProgressMsg = msg;
                });
            m_Success.store(ok);
            m_Done.store(true);
        });
    }

} // namespace Ayaya
