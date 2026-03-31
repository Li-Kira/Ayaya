#pragma once
#include "Renderer/SceneRenderer.hpp"
#include <memory>

namespace Ayaya {

    class FrameDebuggerPanel {
    public:
        FrameDebuggerPanel() = default;

        void SetContext(const std::shared_ptr<SceneRenderer>& renderer) { m_Renderer = renderer; }
        void OnImGuiRender();

        bool IsOpen = false;

    private:
        std::shared_ptr<SceneRenderer> m_Renderer;
        int m_CurrentViewIndex = 0;
    };

}