#include "ayapch.h"
#include "OpenGLContext.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Core/Log.hpp"

namespace Ayaya {

    OpenGLContext::OpenGLContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle) {
        AYAYA_CORE_ERROR("Window handle is null!");
    }

    void OpenGLContext::Init() {
        glfwMakeContextCurrent(m_WindowHandle);
        
        int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        AYAYA_CORE_ERROR("Failed to initialize Glad!");

        AYAYA_CORE_INFO("OpenGL Context Initialized:");
        AYAYA_CORE_INFO("  Vendor:   {0}", (const char*)glGetString(GL_VENDOR));
        AYAYA_CORE_INFO("  Renderer: {0}", (const char*)glGetString(GL_RENDERER));
        AYAYA_CORE_INFO("  Version:  {0}", (const char*)glGetString(GL_VERSION));
    }

    void OpenGLContext::SwapBuffers() {
        glfwSwapBuffers(m_WindowHandle);
    }

}