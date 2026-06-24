#pragma once
#include <string>

namespace Ayaya {

    class PreferencesPanel {
    public:
        PreferencesPanel() = default;

        // 初始化方法（在引擎启动时调用）
        void Init();

        void OnImGuiRender();

        void SetOpen(bool isOpen);
        bool IsOpen() const { return m_IsOpen; }

        // Immediate save (called from PropertiesPanel when SRP pipeline changes)
        void SavePreferences();

        // 【新增】：全局底层渲染配置 (Hardware / Backend)
        int GraphicsAPI = 0; // 0 = OpenGL, 1 = Vulkan, 2 = DirectX 12
        bool EnableVSync = false;

        // 编辑器历史配置
        int MaxUndoSteps = 100;
        
    private:
        void LoadPreferences();

    private:
        bool m_IsOpen = false;
        std::string m_PrefsFilePath = "assets/Editor/settings/EditorPreferences.yaml";
        
        int m_WindowWidth = 1280;
        int m_WindowHeight = 720;
        float m_UIScale = 1.0f;
        float m_ContentScale = 1.0f; // macOS Retina: 2.0, others: 1.0
        int InitialGraphicsAPI = 0;
    };

}