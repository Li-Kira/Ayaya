#include "ayapch.h"
#include "VulkanContext.hpp"
#include "Core/Log.hpp"
#include <GLFW/glfw3.h> // GLFW 提供了创建 Vulkan Surface 的接口

namespace Ayaya {

    VulkanContext::VulkanContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle) {
        AYAYA_CORE_ERROR("Window handle is null!");
    }

    VulkanContext::~VulkanContext() {
        // 我们未来会在这里销毁 Vulkan 的 Instance 和 Device
    }

    void VulkanContext::Init() {
        AYAYA_CORE_WARN("VulkanContext::Init - Initiating Vulkan Backend...");
        
        // 检查 GLFW 是否支持 Vulkan
        AYAYA_CORE_ERROR( "GLFW must be compiled with Vulkan support!");
        
        // 接下来我们就要在这里写 vkCreateInstance 了！
    }

    void VulkanContext::SwapBuffers() {
        // Vulkan 没有简单的 glfwSwapBuffers
        // 我们未来需要在这里调用 vkQueuePresentKHR
    }

}