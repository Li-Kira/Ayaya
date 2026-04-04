#pragma once
#include "Renderer/GraphicsContext.hpp"

struct GLFWwindow;

namespace Ayaya {

    class OpenGLContext : public GraphicsContext {
    public:
        OpenGLContext(GLFWwindow* windowHandle);
        virtual ~OpenGLContext() = default;

        virtual void Init() override;
        virtual void SwapBuffers() override;

    private:
        GLFWwindow* m_WindowHandle;
    };

}