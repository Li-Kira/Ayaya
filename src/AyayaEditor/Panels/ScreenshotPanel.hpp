#pragma once

#include <string>
#include <cstdint>

namespace Ayaya {

    class ScreenshotPanel {
    public:
        ScreenshotPanel() = default;

        // 供 EditorLayer 每帧调用渲染 UI
        void OnImGuiRender();
        
        // 从外部菜单呼出面板
        void Open() { m_ShowPopup = true; }

        // ==========================================
        // 核心解耦接口：供渲染管线消费截图请求
        // ==========================================
        bool ConsumePending() {
            if (m_Pending) {
                m_Pending = false; // 读取后立刻重置状态，防止连续截图
                return true;
            }
            return false;
        }

        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        const std::string& GetPath() const { return m_Path; }

    private:
        bool m_ShowPopup = false;
        bool m_Pending = false;
        
        uint32_t m_Width = 1920;
        uint32_t m_Height = 1080;
        std::string m_Path = "";
    };

}