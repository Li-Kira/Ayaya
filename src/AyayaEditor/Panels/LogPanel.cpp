#include "ayapch.h"
#include "LogPanel.hpp"
#include "Core/Log.hpp"
#include "UI_Widgets.hpp"
#include <spdlog/spdlog.h>
#include <IconsFontAwesome5.h>

namespace Ayaya {

    namespace {
        // spdlog::level::level_enum → 显示用颜色 (对齐控制台 stdout_color 配色)
        ImVec4 LevelColor(int level) {
            switch (level) {
                case spdlog::level::trace:  return ImVec4(0.55f, 0.55f, 0.60f, 1.0f); // 灰白
                case spdlog::level::debug:  return ImVec4(0.40f, 0.75f, 0.85f, 1.0f); // 青
                case spdlog::level::info:   return ImVec4(0.50f, 0.85f, 0.50f, 1.0f); // 绿
                case spdlog::level::warn:   return ImVec4(0.95f, 0.80f, 0.30f, 1.0f); // 黄
                case spdlog::level::err:
                case spdlog::level::critical: return ImVec4(0.95f, 0.40f, 0.40f, 1.0f); // 红
                default:                    return ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
            }
        }

        bool LevelPass(int level, bool showTrace, bool showInfo, bool showWarn, bool showError) {
            return (level == spdlog::level::trace && showTrace) ||
                   (level == spdlog::level::debug && showTrace) ||
                   (level == spdlog::level::info  && showInfo)  ||
                   (level == spdlog::level::warn  && showWarn)  ||
                   (level >= spdlog::level::err   && showError);
        }
    }

    void LogPanel::RenderContent() {
        // ---- 第一行：搜索 + 清空 ----
        ImGui::PushItemWidth(-70.0f);
        ImGui::InputTextWithHint("##LogSearch", "Search...", m_SearchFilter, sizeof(m_SearchFilter));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH " Clear"))
            Log::ClearEntries();

        // ---- 第二行：级别过滤 + 显示选项 ----
        ImGui::Checkbox("Trace", &m_ShowTrace); ImGui::SameLine();
        ImGui::Checkbox("Info",  &m_ShowInfo);  ImGui::SameLine();
        ImGui::Checkbox("Warn",  &m_ShowWarn);  ImGui::SameLine();
        ImGui::Checkbox("Error", &m_ShowError); ImGui::SameLine();
        ImGui::Checkbox("Wrap",  &m_WordWrap);  ImGui::SameLine();
        ImGui::Checkbox("Collapse", &m_CollapseDuplicates); ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
        ImGui::Separator();

        // ---- 构建可见行列表 (含折叠 + 过滤) ----
        auto entries = Log::GetRecentEntries();
        const bool searchActive = (m_SearchFilter[0] != '\0');

        struct VisibleLine {
            const LogEntry* entry;
            size_t          count;   // 折叠后的重复计数
            std::string     text;    // 完整行文本 (不含计数)
        };
        std::vector<VisibleLine> lines;
        lines.reserve(entries.size());

        for (size_t i = 0; i < entries.size(); ) {
            const LogEntry& e = entries[i];

            // 折叠重复：统计与当前条目 (level, category, message) 连续相同的数量
            size_t count = 1;
            if (m_CollapseDuplicates) {
                while (i + count < entries.size() &&
                       entries[i + count].level == e.level &&
                       entries[i + count].category == e.category &&
                       entries[i + count].message == e.message) {
                    ++count;
                }
            }

            std::string full = "[" + e.time + "] [" + e.category + "] " + e.message;
            bool pass = LevelPass(e.level, m_ShowTrace, m_ShowInfo, m_ShowWarn, m_ShowError) &&
                        (!searchActive || full.find(m_SearchFilter) != std::string::npos);
            if (pass)
                lines.push_back({ &e, count, std::move(full) });

            i += count;
        }

        // Copy All 文本：拼接所有可见行
        std::string allText;
        for (const auto& l : lines) {
            if (!allText.empty()) allText += '\n';
            allText += l.text;
        }

        // ---- 日志列表 ----
        ImGui::BeginChild("##LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& l : lines) {
            // 用单调序号做 ID 作用域，避免重复日志文本 / 固定右键菜单 ID 触发冲突
            // 取低 31 位，避免 uint64 → int 溢出 (2048 条窗口内低位仍唯一)
            ImGui::PushID((int)(l.entry->sequence & 0x7FFFFFFF));

            std::string display = l.text;
            if (l.count > 1) display += "  (" + std::to_string(l.count) + ")";

            bool selected = (l.entry->sequence == m_SelectedSeq);

            // 所有日志行统一用等宽字体 JetBrains Mono，时间戳 + 消息列对齐。
            // 多行文本不折行、逐行原样显示；单行长文本仍可按 Word Wrap 折行。
            // Selectable 内部用 RenderTextClipped 不会换行，长文本会被裁掉；改用空标签 Selectable
            // 撑起可点击/高亮区域，文本用 RenderTextWrapped 手动换行渲染。
            ImFont* monoFont = (ImGui::GetIO().Fonts->Fonts.Size > 2) ? ImGui::GetIO().Fonts->Fonts[2] : nullptr;  // JetBrains Mono
            if (monoFont) ImGui::PushFont(monoFont);

            bool isMultiline = (l.text.find('\n') != std::string::npos);
            float availW = ImGui::GetContentRegionAvail().x;
            float wrapW  = (!isMultiline && m_WordWrap) ? availW : -1.0f;
            ImVec2 textSize = ImGui::CalcTextSize(display.c_str(), NULL, false, wrapW);
            float rowW = (!isMultiline && m_WordWrap) ? availW : std::max(textSize.x, availW);

            ImVec2 lineMin = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::Selectable("##logline", selected, 0, ImVec2(rowW, textSize.y));

            // 只有真正的像素画 (含连续 @ 网格) 用粉色 RGB(255,150,200)，其余按日志级别着色。
            // Vulkan 校验层等多行警告虽含 \n，但不是像素画，应保持级别色。
            bool isPixelArt = (l.text.find("@@@@@@@@@@") != std::string::npos);
            ImVec4 textColor = isPixelArt ? ImVec4(1.0f, 150.0f / 255.0f, 200.0f / 255.0f, 1.0f)
                                          : LevelColor(l.entry->level);
            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            ImGui::RenderTextWrapped(lineMin, display.c_str(), display.c_str() + display.size(), wrapW);
            ImGui::PopStyleColor();

            if (clicked) {
                m_SelectedSeq  = l.entry->sequence;
                m_SelectedText = l.text;
            }

            if (monoFont) ImGui::PopFont();

            // 右键菜单 (绑定到当前行；无参 → 复用上一 item 的唯一 ID)
            UI::PushPopupStyles(220.0f);
            if (ImGui::BeginPopupContextItem()) {
                if (UI::DrawNativeMenuItem("Copy", ICON_FA_COPY))
                    ImGui::SetClipboardText(l.text.c_str());
                if (UI::DrawNativeMenuItem("Copy All", ICON_FA_COPY))
                    ImGui::SetClipboardText(allText.c_str());
                UI::MenuSeparator();
                if (UI::DrawNativeMenuItem("Clear", ICON_FA_TRASH))
                    Log::ClearEntries();
                ImGui::EndPopup();
            }
            UI::PopPopupStyles();

            ImGui::PopID();
        }

        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();

        // Ctrl/Cmd+C 复制选中行
        bool copyModifier = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        bool copyHotkey = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                          copyModifier && ImGui::IsKeyPressed(ImGuiKey_C, false);
        if (copyHotkey && m_SelectedSeq != UINT64_MAX)
            ImGui::SetClipboardText(m_SelectedText.c_str());
    }

} // namespace Ayaya
