#pragma once

#include "KeyCodes.hpp"
#include <utility>

namespace Ayaya {

    class Input {
    public:
        // 静态公共接口，供 CameraController 等直接使用
        static bool IsKeyPressed(KeyCode key) { return s_Instance->IsKeyPressedImpl(key); }
        static bool IsMouseButtonPressed(MouseCode button) { return s_Instance->IsMouseButtonPressedImpl(button); }
        static std::pair<float, float> GetMousePosition() { return s_Instance->GetMousePositionImpl(); }
        static float GetMouseX() { return s_Instance->GetMouseXImpl(); }
        static float GetMouseY() { return s_Instance->GetMouseYImpl(); }

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