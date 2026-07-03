#pragma once
#include "Renderer/GraphicsContext.hpp"
#include "VulkanBindlessManager.hpp"
#include "VulkanGeometryPool.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <optional>
#include <set>
#include <vector>
#include <algorithm>
#include <mutex>
#include <functional>

struct GLFWwindow;

namespace Ayaya {

    // GPU capability flags queried at device selection time.
    // Passes and subsystems use these to route between fast-path and fallback implementations.
    struct VulkanCapabilities {
        bool HasDrawIndirectCount = false;
        bool HasBindlessTextures = false;
        bool HasHardwarePCF = false;  // reserved for future sampler2DShadow filtering support
    };

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
        void SetVSync(bool vsync);

        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkFormat FindDepthFormat();

        inline uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; }
        inline uint32_t GetFramesInFlight() const { return m_FramesInFlight; }

        // ---- GPU Timestamp Queries ----
        bool IsTimestampSupported() const { return m_TimestampValidBits > 0; }
        float GetTimestampPeriod() const { return m_TimestampPeriod; }
        uint64_t GetTimestampMask() const { return m_TimestampMask; }
        VkQueryPool GetTimestampPool() const { return m_TimestampPool; }
        const std::vector<uint64_t>& GetTimestampResults() const { return m_TimestampResults; }
        void ReadTimestampResults();
        // Allocate consecutive query indices for one pass (returns start index).
        uint32_t AllocTimestampSlot();

        inline VulkanBindlessManager& GetBindlessManager() { return m_BindlessManager; }
        inline VkDescriptorSetLayout GetBindlessLayout() const { return m_BindlessManager.GetLayout(); }
        inline VkDescriptorSet GetBindlessSet() const { return m_BindlessManager.GetSet(); }
        inline GlobalGeometryPool& GetGeometryPool() { return m_GeometryPool; }

        // GPU capabilities queried at device creation (see VulkanCapabilities)
        const VulkanCapabilities& GetCapabilities() const { return m_Capabilities; }

        // Default bindless texture indices (fixed, never recycled)
        uint32_t GetWhiteTextureIndex() const         { return VulkanBindlessManager::kWhiteIndex; }
        uint32_t GetBlackTextureIndex() const         { return VulkanBindlessManager::kBlackIndex; }
        uint32_t GetDefaultNormalIndex() const        { return VulkanBindlessManager::kDefaultNormalIndex; }

        // Deferred bindless index release (3-frame delay for GPU safety)
        void QueueDeferredBindlessRelease(uint32_t index);
        void ProcessDeferredBindlessReleases();

        // General-purpose deferred resource destruction queue.
        // Resources are kept alive for 3 frames after being queued, then destroyed
        // in BeginFrame() after the fence wait confirms the GPU is done with them.
        struct DeferredResource {
            std::function<void()> destroy;  // Lambda — MUST only capture Vulkan handles by VALUE
            int framesRemaining = 3;
        };
        void QueueDeferredResource(DeferredResource&& resource);
        void ProcessDeferredResources(bool forceAll = false);

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
        bool m_VSync = false;
        bool m_VSyncPending = false;  // deferred swapchain rebuild flag

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

        VulkanCapabilities     m_Capabilities;
        VulkanBindlessManager m_BindlessManager;
        GlobalGeometryPool    m_GeometryPool;

        // Default 1x1 textures for bindless fallback (fixed indices 1-3)
        VkImage        m_DefaultWhiteImage       = VK_NULL_HANDLE;
        VkImageView    m_DefaultWhiteView        = VK_NULL_HANDLE;
        VkSampler      m_DefaultWhiteSampler     = VK_NULL_HANDLE;
        VmaAllocation  m_DefaultWhiteAllocation  = VK_NULL_HANDLE;

        VkImage        m_DefaultBlackImage       = VK_NULL_HANDLE;
        VkImageView    m_DefaultBlackView        = VK_NULL_HANDLE;
        VkSampler      m_DefaultBlackSampler     = VK_NULL_HANDLE;
        VmaAllocation  m_DefaultBlackAllocation  = VK_NULL_HANDLE;

        VkImage        m_DefaultNormalImage      = VK_NULL_HANDLE;
        VkImageView    m_DefaultNormalView       = VK_NULL_HANDLE;
        VkSampler      m_DefaultNormalSampler    = VK_NULL_HANDLE;
        VmaAllocation  m_DefaultNormalAllocation = VK_NULL_HANDLE;

        void CreateDefaultBindlessTextures();
        void DestroyDefaultBindlessTextures();

        // Deferred bindless index release (3-frame delay for GPU safety)
        struct DeferredBindlessRelease {
            uint32_t Index;
            uint32_t FramesRemaining = 3;
        };
        std::vector<DeferredBindlessRelease> m_DeferredBindlessReleases;

        std::vector<DeferredResource> m_DeferredResources;
        std::mutex m_DeferredResourcesMutex;

        // GPU timestamp queries (16 passes × 2 slots × 3 frames-in-flight)
        static constexpr uint32_t kMaxTimestampQueries = 96;
        // Results buffer: 2× query count (result + availability interleaved, stride=16)
        static constexpr uint32_t kMaxTimestampResults  = kMaxTimestampQueries * 2;
        VkQueryPool m_TimestampPool = VK_NULL_HANDLE;
        std::vector<uint64_t> m_TimestampResults;  // [r0, avail0, r1, avail1, ...]
        uint32_t m_TimestampValidBits = 0;
        float    m_TimestampPeriod    = 0.0f;
        uint64_t m_TimestampMask      = 0;
        uint32_t m_TimestampSlotsUsed = 0;
        uint32_t m_LastFrameSlotCount = 0;
        uint32_t m_TimestampNotReadyCount = 0;  // throttled VK_NOT_READY logging

        // Debug messenger for Vulkan validation layer output
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        void SetupDebugMessenger();
        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT type,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData);

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
