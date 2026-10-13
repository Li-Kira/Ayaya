#include "ayapch.h"
#include "EditorBottomPanel.hpp"

namespace Ayaya {

    void EditorBottomPanel::RenderContent() {
        switch (m_ActiveTab) {
            case TabContentBrowser: m_ContentBrowser.RenderContent(); break;
            case TabLog:            m_LogPanel.RenderContent();       break;
            case TabTimeline:       m_Timeline.RenderContent();       break;
            default: break;
        }
    }

    const char* EditorBottomPanel::ActiveTabTitle() const {
        switch (m_ActiveTab) {
            case TabLog:      return "Output Log";
            case TabTimeline: return "Timeline";
            default:          return "Content Browser";
        }
    }

    void EditorBottomPanel::ToggleTab(int tab) {
        if (m_Open && m_ActiveTab == tab) {
            m_Open = false;
        } else {
            m_Open = true;
            m_ActiveTab = tab;
        }
    }

    void EditorBottomPanel::Close() {
        m_Open = false;
    }

} // namespace Ayaya
