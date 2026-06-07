#pragma once
#include "Renderer/GraphicsContext.hpp"
#include "VulkanBindlessManager.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <optional>
#include <set>
#include <vector>
#include <algorithm>

struct GLFWwindow;

namespace Ayaya {

    struct QueueFamilyIndices {
        std::optional<uint32_t> GraphicsFamily;
        std::optional<uint32_t> PresentFamily;

        bool IsComplete() {
            return GraphicsFamily.has_value() && PresentFamily.has_value();
        }
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR Capabilities;
        std::vector<VkSurfaceFormatKHR> Formats;
        std::vector<VkPresentModeKHR> PresentModes;
    };

    // Vulkan 1.3 Dynamic Rendering — 不再使用 VkRenderPass / VkFramebuffer
    class VulkanContext : public GraphicsContext {
    public:
        VulkanContext(GLFWwindow* windowHandle);
        virtual ~VulkanContext();

        virtual void Init() override;
        virtual void SwapBuffers() override;

        inline VkInstance GetInstance() const { return m_Instance; }
        inline VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        inline VkDevice GetDevice() const { return m_Device; }
        inline VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        inline VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }
        inline uint32_t GetImageCount() const { return static_cast<uint32_t>(m_SwapChainImages.size()); }
        inline uint32_t GetMinImageCount() const { return 2; }
        inline uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
        inline VmaAllocator GetAllocator() const { return m_Allocator; }

        virtual void BeginFrame() override;
        inline VkExtent2D GetSwapChainExtent() const { return m_SwapChainExtent; }
        inline VkFormat GetSwapChainImageFormat() const { return m_SwapChainImageFormat; }
        inline VkImageView GetCurrentImageView() const { return m_SwapChainImageViews[m_ImageIndex]; }
        inline VkImage GetCurrentImage() const { return m_SwapChainImages[m_ImageIndex]; }
        inline VkCommandBuffer GetCurrentCommandBuffer() const { return m_CommandBuffers[m_CurrentFrame]; }

        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

        void RecreateSwapChain();

        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkFormat FindDepthFormat();

        inline uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; }
        inline uint32_t GetFramesInFlight() const { return m_FramesInFlight; }

        inline VulkanBindlessManager& GetBindlessManager() { return m_BindlessManager; }
        inline VkDescriptorSetLayout GetBindlessLayout() const { return m_BindlessManager.GetLayout(); }
        inline VkDescriptorSet GetBindlessSet() const { return m_BindlessManager.GetSet(); }

    private:
        GLFWwindow* m_WindowHandle;

        VkInstance m_Instance = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;

        VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
        std::vector<VkImage> m_SwapChainImages;
        VkFormat m_SwapChainImageFormat;
        VkExtent2D m_SwapChainExtent;
        std::vector<VkImageView> m_SwapChainImageViews;

        uint32_t m_FramesInFlight = 3;

        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        VkCommandPool m_OneTimeCommandPool = VK_NULL_HANDLE;

        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        std::vector<VkFence> m_InFlightFences;

        std::vector<VkCommandBuffer> m_CommandBuffers;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

        VmaAllocator m_Allocator = VK_NULL_HANDLE;

        uint32_t m_CurrentFrame = 0;
        uint32_t m_ImageIndex = 0;
        uint32_t m_GraphicsQueueFamily = 0;

        VulkanBindlessManager m_BindlessManager;

        void CreateSurface();
        void PickPhysicalDevice();
        bool IsDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
        void CreateLogicalDevice();

        SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
        VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
        void CreateSwapChain();
        void CreateImageViews();

        void CreateCommandPool();
        void CreateSyncObjects();

        void AllocateCommandBuffers();
        void CreateDescriptorPool();

        void CleanupSwapChain();
    };
}
