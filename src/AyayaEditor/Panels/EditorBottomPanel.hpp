#pragma once

#include "ContentBrowserPanel.hpp"
#include "TimelinePanel.hpp"
#include "LogPanel.hpp"

namespace Ayaya {

    // ==========================================
    // EditorBottomPanel — 底部面板 (Unreal Content Drawer 逻辑)
    // 面板本体是 EditorLayer 里的一个浮动覆盖窗口 (NoDocking，覆盖在视口上方)，
    // 本类只负责：维护当前激活 Tab + 渲染当前面板内容。
    // 底部栏按钮就是唯一的 tab 条 (无窗口内 TabBar)。
    // ==========================================
    class EditorBottomPanel {
    public:
        enum Tab {
            TabContentBrowser = 0,
            TabLog,
            TabTimeline
        };

        EditorBottomPanel(ContentBrowserPanel& contentBrowser, TimelinePanel& timeline)
            : m_ContentBrowser(contentBrowser), m_Timeline(timeline) {}

        void RenderContent();        // 只画当前面板内容 (无 Begin/无 TabBar)
        const char* ActiveTabTitle() const;  // 当前面板名 (用作窗口标题)

        void ToggleTab(int tab);     // 已激活则收起，否则打开并激活 (底部栏按钮语义)
        void Close();                // 收起 (标题条 X 按钮)

        bool IsOpen() const { return m_Open; }
        int  ActiveTab() const { return m_ActiveTab; }
        bool IsTabActive(int tab) const { return m_Open && m_ActiveTab == tab; }

    private:
        ContentBrowserPanel& m_ContentBrowser;
        TimelinePanel&       m_Timeline;
        LogPanel             m_LogPanel;

        bool m_Open = false;
        int  m_ActiveTab = TabContentBrowser;
    };

} // namespace Ayaya
