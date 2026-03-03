#pragma once

#include "Engine/Core/Layer.hpp"
#include "Engine/Events/ApplicationEvent.hpp"
#include "Engine/Events/KeyEvent.hpp"
#include "Engine/Events/MouseEvent.hpp"

namespace Ayaya {

    class ImGuiLayer : public Layer {
    public:
        ImGuiLayer();
        ~ImGuiLayer();

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnEvent(Event& e) override;

        void Begin(); // 每一帧渲染前调用
        void End();   // 每一帧渲染后调用

    private:
        float m_Time = 0.0f;
    };

}