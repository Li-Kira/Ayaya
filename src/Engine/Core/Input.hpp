#pragma once

#include "KeyCodes.hpp"
#include <utility>

namespace Ayaya {

    class Input {
    public:
        // 检测按键是否按下
        static bool IsKeyPressed(KeyCode key);

        // 检测鼠标按键是否按下
        static bool IsMouseButtonPressed(KeyCode button);

        // 获取鼠标位置
        static std::pair<float, float> GetMousePosition();
        static float GetMouseX();
        static float GetMouseY();
    };

}