#pragma once
#include "Renderer/GraphicsContext.hpp"
#include <vulkan/vulkan.h>
#include <optional>
#include <set> // 【新增】：用于处理不重复的队列族

struct GLFWwindow;

namespace Ayaya {

    struct QueueFamilyIndices {
        std::optional<uint32_t> GraphicsFamily; 
        std::optional<uint32_t> PresentFamily;  

        bool IsComplete() {
            return GraphicsFamily.has_value() && PresentFamily.has_value();
        }
    };

    class VulkanContext : public GraphicsContext {
    public:
        VulkanContext(GLFWwindow* windowHandle);
        virtual ~VulkanContext();

        virtual void Init() override;
        virtual void SwapBuffers() override;

    private:
        GLFWwindow* m_WindowHandle;
        
        VkInstance m_Instance = VK_NULL_HANDLE; 
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;       
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE; 

        // ==========================================
        // 【新增】：逻辑设备与核心命令队列
        // ==========================================
        VkDevice m_Device = VK_NULL_HANDLE;         // 逻辑设备（我们在 GPU 内部的“指挥部”）
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;   // 负责画图的队列
        VkQueue m_PresentQueue = VK_NULL_HANDLE;    // 负责把图送到屏幕上的队列

        void CreateSurface();
        void PickPhysicalDevice();
        bool IsDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
        
        // 【新增】：创建逻辑设备的函数声明
        void CreateLogicalDevice(); 
    };

}