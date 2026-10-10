#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

namespace Ayaya {

    class Scene;

    // ==========================================
    // ExportPanel — 游戏导出面板
    // 主题化弹窗 (对齐 Properties/ContentBrowser)，异步导出 + 进度条。
    // ==========================================
    class ExportPanel {
    public:
        ExportPanel() = default;
        ~ExportPanel() { if (m_Worker.joinable()) m_Worker.join(); }

        void OnImGuiRender(const std::shared_ptr<Scene>& activeScene);
        void Open() { m_ShowPopup = true; }

    private:
        void StartExport(const std::shared_ptr<Scene>& activeScene);

        bool m_ShowPopup = false;

        char        m_GameName[256] = "Game";
        int         m_Platform = 0;       // 0=macOS, 1=Windows
        int         m_GraphicsAPI = 0;    // 0=Vulkan, 1=OpenGL
        int         m_Width = 1280;
        int         m_Height = 720;
        bool        m_VSync = false;
        std::string m_DestDir;

        // 异步导出状态 (后台线程写，主线程读)
        std::atomic<bool>  m_Exporting{ false };
        std::atomic<bool>  m_Done{ false };
        std::atomic<bool>  m_Success{ false };
        std::atomic<float> m_Progress{ 0.0f };
        std::mutex         m_Mutex;
        std::string        m_ProgressMsg;
        std::string        m_Result;
        std::thread        m_Worker;
    };

} // namespace Ayaya
