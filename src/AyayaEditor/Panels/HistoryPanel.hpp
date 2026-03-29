#pragma once
#include "Engine/Core/CommandHistory.hpp"

namespace Ayaya {

    class HistoryPanel {
    public:
        HistoryPanel() = default;

        // 面板的绘制入口
        void OnImGuiRender();

        // 控制面板开关的公共状态
        bool IsOpen = false; 
    };

}