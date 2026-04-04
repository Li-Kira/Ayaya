#pragma once
#include "Renderer/GraphicsContext.hpp"

struct GLFWwindow;

namespace Ayaya {

    class VulkanContext : public GraphicsContext {
    public:
        VulkanContext(GLFWwindow* windowHandle);
        virtual ~VulkanContext();

        virtual void Init() override;
        virtual void SwapBuffers() override;

    private:
        GLFWwindow* m_WindowHandle;
        
        // Vulkan 的核心句柄即将在这里诞生...
        // VkInstance m_Instance;
        // VkPhysicalDevice m_PhysicalDevice;
        // VkDevice m_Device;
    };

}