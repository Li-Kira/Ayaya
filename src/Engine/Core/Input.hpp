#pragma once

#include "KeyCodes.hpp"
#include <utility>

namespace Ayaya {

    class Input {
    public:
        // 静态公共接口，供 CameraController 等直接使用
        // Returns safe defaults when no platform implementation is active (headless/test).
        static bool IsKeyPressed(KeyCode key) {
            return s_Instance ? s_Instance->IsKeyPressedImpl(key) : false;
        }
        static bool IsMouseButtonPressed(MouseCode button) {
            return s_Instance ? s_Instance->IsMouseButtonPressedImpl(button) : false;
        }
        static std::pair<float, float> GetMousePosition() {
            return s_Instance ? s_Instance->GetMousePositionImpl() : std::make_pair(0.0f, 0.0f);
        }
        static float GetMouseX() {
            return s_Instance ? s_Instance->GetMouseXImpl() : 0.0f;
        }
        static float GetMouseY() {
            return s_Instance ? s_Instance->GetMouseYImpl() : 0.0f;
        }

    protected:
        // 平台相关的具体实现接口
        virtual bool IsKeyPressedImpl(KeyCode key) = 0;
        virtual bool IsMouseButtonPressedImpl(MouseCode button) = 0;
        virtual std::pair<float, float> GetMousePositionImpl() = 0;
        virtual float GetMouseXImpl() = 0;
        virtual float GetMouseYImpl() = 0;

    private:
        static Input* s_Instance; // 指向具体平台实现的单例
    };

}