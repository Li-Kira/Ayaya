#pragma once
#include <string>

namespace Ayaya {

    class PreferencesPanel {
    public:
        PreferencesPanel() = default;

        // 新增：初始化方法（在引擎启动时调用）
        void Init();

        void OnImGuiRender();

        void SetOpen(bool isOpen);
        bool IsOpen() const { return m_IsOpen; }

        int ToneMappingType = 0; 
        float Exposure = 1.0f;
    private:
        // 新增：保存和读取配置的私有方法
        void SavePreferences();
        void LoadPreferences();

    private:
        bool m_IsOpen = false;
        // 配置文件的保存路径
        std::string m_PrefsFilePath = "assets/Editor/settings/EditorPreferences.yaml";
        
        int m_WindowWidth = 1280;
        int m_WindowHeight = 720;
        float m_UIScale = 1.0f;

        
    };

}