#pragma once
#include "Renderer/GraphicsContext.hpp"
#include <vulkan/vulkan.h>
#include <optional>
#include <set>
#include <vector> // 确保引入 vector
#include <algorithm> // 确保引入 algorithm (后面 clamp 要用)

struct GLFWwindow;

namespace Ayaya {

    struct QueueFamilyIndices {
        std::optional<uint32_t> GraphicsFamily; 
        std::optional<uint32_t> PresentFamily;  

        bool IsComplete() {
            return GraphicsFamily.has_value() && PresentFamily.has_value();
        }
    };

    // 【新增】：用于存放查询到的交换链支持细节
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR Capabilities;
        std::vector<VkSurfaceFormatKHR> Formats;
        std::vector<VkPresentModeKHR> PresentModes;
    };

    class VulkanContext : public GraphicsContext {
    public:
        VulkanContext(GLFWwindow* windowHandle);
        virtual ~VulkanContext();

        virtual void Init() override;
        virtual void SwapBuffers() override;

        // ==========================================
        // 【新增】：向整个引擎暴露 Vulkan 核心句柄！
        // 马上我们的 ImGuiLayer 和未来的 Renderer 都会疯狂调用它们
        // ==========================================
        inline VkInstance GetInstance() const { return m_Instance; }
        inline VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        inline VkDevice GetDevice() const { return m_Device; }
        inline VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        inline VkRenderPass GetRenderPass() const { return m_RenderPass; }
        inline VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }
        inline uint32_t GetImageCount() const { return static_cast<uint32_t>(m_SwapChainImages.size()); }
        inline uint32_t GetMinImageCount() const { return 2; } // 通常双重缓冲最少 2 张
        inline uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }

        // ==========================================
        // 【新增】：核心渲染大循环与单次命令接口
        // ==========================================
        void BeginFrame(); 
        inline VkCommandBuffer GetCurrentCommandBuffer() const { return m_CommandBuffers[m_CurrentFrame]; }

        // 供外部（如 ImGui）向显卡一次性传输数据的通道
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
        
    private:
        GLFWwindow* m_WindowHandle;
        
        VkInstance m_Instance = VK_NULL_HANDLE; 
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;       
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE; 
        VkDevice m_Device = VK_NULL_HANDLE;         
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;   
        VkQueue m_PresentQueue = VK_NULL_HANDLE;    

        // ==========================================
        // 【新增】：Swapchain 核心句柄与属性
        // ==========================================
        VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
        std::vector<VkImage> m_SwapChainImages;      // 显卡分配的真正画布 (内存)
        VkFormat m_SwapChainImageFormat;             // 画布的色彩格式 (比如 RGBA8)
        VkExtent2D m_SwapChainExtent;                // 画布的分辨率大小
        std::vector<VkImageView> m_SwapChainImageViews; // 图像视图 (告诉 Vulkan 如何看待这些内存)
        // ==========================================
        // 【新增】：Render Pass 与 帧缓冲
        // ==========================================
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_SwapChainFramebuffers;

        // ==========================================
        // 【新增】：命令池与同步原语 (双重缓冲)
        // ==========================================
        // 允许 CPU 最多超前 GPU 准备几帧的数据 (通常是 2 帧，也就是常说的双重缓冲)
        const int MAX_FRAMES_IN_FLIGHT = 2;

        VkCommandPool m_CommandPool = VK_NULL_HANDLE;

        // 因为有两帧在同时流转，所以同步对象也需要准备两套！
        std::vector<VkSemaphore> m_ImageAvailableSemaphores; // 信号量：画布准备好了吗？
        std::vector<VkSemaphore> m_RenderFinishedSemaphores; // 信号量：图画完了吗？
        std::vector<VkFence> m_InFlightFences;               // 围栏：CPU 需要等待 GPU 吗？

        // ==========================================
        // 【新增】：命令缓冲与描述符池
        // ==========================================
        std::vector<VkCommandBuffer> m_CommandBuffers;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

        // 【新增】：用于追踪当前绘制到了哪一帧，以及 Swapchain 给的是哪张图
        uint32_t m_CurrentFrame = 0;
        uint32_t m_ImageIndex = 0;
        uint32_t m_GraphicsQueueFamily = 0;

        void CreateSurface();
        void PickPhysicalDevice();
        bool IsDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
        void CreateLogicalDevice(); 

        // 【新增】：交换链相关辅助函数
        SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
        VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
        void CreateSwapChain();
        void CreateImageViews();

        // 【新增】：创建渲染通道与帧缓冲的函数
        void CreateRenderPass();
        void CreateFramebuffers();

        // 【新增】：创建池和同步对象的函数声明
        void CreateCommandPool();
        void CreateSyncObjects();

        // 【新增】：分配函数声明
        void AllocateCommandBuffers();
        void CreateDescriptorPool();
    };

}