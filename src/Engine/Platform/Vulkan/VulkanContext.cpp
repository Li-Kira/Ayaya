#include "ayapch.h"
#include "VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Core/Log.hpp"
#include <GLFW/glfw3.h>
#include <vector>

namespace Ayaya {

    VulkanContext::VulkanContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle) {
        AYAYA_CORE_ASSERT(windowHandle, "Window handle is null!");
    }

    VulkanContext::~VulkanContext() {
        if (m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Device);
        }

        m_GeometryPool.Shutdown();
        DestroyDefaultBindlessTextures();
        m_BindlessManager.Shutdown(m_Device);

        if (m_Allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_Allocator);
            AYAYA_CORE_INFO("VMA Allocator destroyed.");
        }

        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
            AYAYA_CORE_INFO("Vulkan Descriptor Pool destroyed.");
        }

        if (m_CommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        }
        if (m_OneTimeCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_Device, m_OneTimeCommandPool, nullptr);
        }
        if (m_TimestampPool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(m_Device, m_TimestampPool, nullptr);
        }

        CleanupSwapChain();  // destroys swapchain + image views

        // Destroy sync objects (not in CleanupSwapChain — presentation engine may
        // hold stale references across swapchain recreations; safe to destroy only at shutdown)
        for (size_t i = 0; i < m_ImageAvailableSemaphores.size(); i++) {
            if (m_ImageAvailableSemaphores[i]) vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
            if (m_RenderFinishedSemaphores[i])  vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
        }
        for (size_t i = 0; i < m_InFlightFences.size(); i++) {
            if (m_InFlightFences[i]) vkDestroyFence(m_Device, m_InFlightFences[i], nullptr);
        }
        m_ImageAvailableSemaphores.clear();
        m_RenderFinishedSemaphores.clear();
        m_InFlightFences.clear();

        if (m_Device != VK_NULL_HANDLE) {
            vkDestroyDevice(m_Device, nullptr);
            AYAYA_CORE_INFO("Vulkan Logical Device destroyed.");
        }

        if (m_Surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        }

        if (m_Instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_Instance, nullptr);
            AYAYA_CORE_INFO("Vulkan Instance destroyed.");
        }
    }

    void VulkanContext::Init() {
        AYAYA_CORE_INFO("VulkanContext::Init - Initiating Vulkan 1.3 Backend (Dynamic Rendering)...");
        AYAYA_CORE_ASSERT(glfwVulkanSupported(), "GLFW must be compiled with Vulkan support!");

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Ayaya Editor";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Ayaya Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

#ifdef __APPLE__
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        // GPU-Assisted Validation (only if VK_EXT_validation_features is available)
        VkValidationFeaturesEXT gpuAssisted{};
        gpuAssisted.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        gpuAssisted.enabledValidationFeatureCount = 1;
        VkValidationFeatureEnableEXT feature = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;
        gpuAssisted.pEnabledValidationFeatures = &feature;
        bool hasGPUAssisted = false;
        {
            uint32_t extCount = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> availExts(extCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &extCount, availExts.data());
            for (auto& e : availExts) {
                if (strcmp(e.extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0) {
                    hasGPUAssisted = true;
                    break;
                }
            }
        }
        if (hasGPUAssisted) {
            extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            gpuAssisted.pNext = createInfo.pNext;
            createInfo.pNext = &gpuAssisted;
        }

        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan Instance!");

        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = m_PhysicalDevice;
        allocatorInfo.device = m_Device;
        allocatorInfo.instance = m_Instance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;

        VkResult vmaResult = vmaCreateAllocator(&allocatorInfo, &m_Allocator);
        AYAYA_CORE_ASSERT(vmaResult == VK_SUCCESS, "Failed to create VMA Allocator!");

        CreateSwapChain();
        CreateImageViews();
        CreateCommandPool();
        CreateSyncObjects();
        AllocateCommandBuffers();
        CreateDescriptorPool();

        VkPhysicalDeviceDescriptorIndexingProperties indexingProps{};
        indexingProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &indexingProps;
        vkGetPhysicalDeviceProperties2(m_PhysicalDevice, &props2);

        // Reserve headroom for non-bindless sampler bindings in other descriptor sets
        // (e.g., WBOIT set=3 IBL, Lighting set=1 GBuffer attachments).
        // The per-stage and per-set limits count ALL sampler bindings across ALL sets.
        constexpr uint32_t kSamplerHeadroom = 32;
        uint32_t maxBindless = std::min({
            indexingProps.maxPerStageDescriptorUpdateAfterBindSamplers,
            indexingProps.maxDescriptorSetUpdateAfterBindSamplers,
            100000u
        });
        if (maxBindless > kSamplerHeadroom)
            maxBindless -= kSamplerHeadroom;
        else
            maxBindless = std::max(maxBindless, 1u);  // at least 1

        AYAYA_CORE_INFO("GPU max update-after-bind samplers: perStage={0}, perSet={1}",
            indexingProps.maxPerStageDescriptorUpdateAfterBindSamplers,
            indexingProps.maxDescriptorSetUpdateAfterBindSamplers);
        AYAYA_CORE_INFO("Bindless texture array capacity: {0}", maxBindless);

        m_BindlessManager.Init(m_Device, maxBindless);
        CreateDefaultBindlessTextures();
        m_GeometryPool.Init(m_Device, m_Allocator);
    }

    void VulkanContext::CreateSurface() {
        VkResult result = glfwCreateWindowSurface(m_Instance, m_WindowHandle, nullptr, &m_Surface);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Window Surface!");
    }

    void VulkanContext::PickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
        AYAYA_CORE_ASSERT(deviceCount > 0, "Failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

        for (const auto& device : devices) {
            if (IsDeviceSuitable(device)) {
                m_PhysicalDevice = device;
                break;
            }
        }
        AYAYA_CORE_ASSERT(m_PhysicalDevice != VK_NULL_HANDLE, "Failed to find a suitable GPU!");

        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &deviceProperties);
        AYAYA_CORE_INFO("Selected GPU: {0}", deviceProperties.deviceName);
    }

    bool VulkanContext::IsDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = FindQueueFamilies(device);
        return indices.IsComplete();
    }

    QueueFamilyIndices VulkanContext::FindQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.GraphicsFamily = i;
            }
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
            if (presentSupport) indices.PresentFamily = i;
            if (indices.IsComplete()) break;
            i++;
        }
        return indices;
    }

    void VulkanContext::CreateLogicalDevice() {
        QueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice);
        m_GraphicsQueueFamily = indices.GraphicsFamily.value();

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = { indices.GraphicsFamily.value(), indices.PresentFamily.value() };

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        // ── Query supported Vulkan 1.2 features ──
        VkPhysicalDeviceVulkan12Features availableVk12{};
        availableVk12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 availableFeatures2{};
        availableFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        availableFeatures2.pNext = &availableVk12;
        vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &availableFeatures2);

        // Populate engine capability flags
        m_Capabilities.HasDrawIndirectCount = (availableVk12.drawIndirectCount == VK_TRUE);
        m_Capabilities.HasBindlessTextures  = (availableVk12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE);
        // Hardware PCF (sampler2DShadow + comparison sampler): always available on desktop GPUs.
        // On MoltenVK (macOS), hardware PCF requires mutableComparisonSamplers portability feature,
        // which is unavailable on M1/M2. Use manual PCF fallback on Apple Silicon.
#ifdef __APPLE__
        m_Capabilities.HasHardwarePCF = false;
#else
        m_Capabilities.HasHardwarePCF = true;
#endif

        AYAYA_CORE_INFO("GPU Capabilities: drawIndirectCount={0}, bindless={1}, hardwarePCF={2}",
            m_Capabilities.HasDrawIndirectCount, m_Capabilities.HasBindlessTextures,
            m_Capabilities.HasHardwarePCF);

        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.independentBlend = VK_TRUE;               // WBOIT per-attachment blend
        deviceFeatures.multiDrawIndirect = VK_TRUE;              // GDR: drawCount > 1
        deviceFeatures.drawIndirectFirstInstance = VK_TRUE;      // GDR: firstInstance != 0 per draw
        deviceFeatures.depthBiasClamp = VK_TRUE;                 // Shadow: clamp slope-scaled bias

        // Vulkan 1.2 features (bindless descriptors + optional drawIndirectCount)
        VkPhysicalDeviceVulkan12Features vk12Features{};
        vk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vk12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        vk12Features.descriptorBindingPartiallyBound = VK_TRUE;
        vk12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
        vk12Features.runtimeDescriptorArray = VK_TRUE;
        vk12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        vk12Features.drawIndirectCount = m_Capabilities.HasDrawIndirectCount ? VK_TRUE : VK_FALSE;

        // Vulkan 1.3 feature: dynamic rendering
        VkPhysicalDeviceVulkan13Features vk13Features{};
        vk13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vk13Features.dynamicRendering = VK_TRUE;
        vk13Features.pNext = nullptr;
        vk12Features.pNext = &vk13Features;

        std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
#ifdef __APPLE__
        deviceExtensions.push_back("VK_KHR_portability_subset");
#endif

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &vk12Features;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        VkResult result = vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create logical device!");

        vkGetDeviceQueue(m_Device, indices.GraphicsFamily.value(), 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, indices.PresentFamily.value(), 0, &m_PresentQueue);

        // ---- GPU Timestamp Query Pool ----
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);
        m_TimestampPeriod = props.limits.timestampPeriod > 0.0f
            ? props.limits.timestampPeriod : 1.0f;

        // Vulkan 1.4+: timestampValidBits moved from VkPhysicalDeviceLimits to
        // VkQueueFamilyProperties.  Query the graphics queue family for it.
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfProps(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &qfCount, qfProps.data());
        m_TimestampValidBits = qfProps[m_GraphicsQueueFamily].timestampValidBits; // nanoseconds per tick
        m_TimestampMask = (m_TimestampValidBits == 64)
            ? ~0ULL : ((1ULL << m_TimestampValidBits) - 1);

        if (m_TimestampValidBits > 0) {
            VkQueryPoolCreateInfo qpCI{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            qpCI.queryType  = VK_QUERY_TYPE_TIMESTAMP;
            qpCI.queryCount = kMaxTimestampQueries;
            vkCreateQueryPool(m_Device, &qpCI, nullptr, &m_TimestampPool);
            m_TimestampResults.resize(kMaxTimestampResults);
            AYAYA_CORE_INFO("GPU Timestamp pool created: validBits={0} period={1}ns",
                m_TimestampValidBits, m_TimestampPeriod);
        } else {
            AYAYA_CORE_WARN("GPU Timestamps NOT supported (validBits=0)");
        }
    }

    SwapChainSupportDetails VulkanContext::QuerySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.Capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.Formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.Formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.PresentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, details.PresentModes.data());
        }
        return details;
    }

    VkSurfaceFormatKHR VulkanContext::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        return availableFormats[0];
    }

    VkPresentModeKHR VulkanContext::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        if (m_VSync) {
            return VK_PRESENT_MODE_FIFO_KHR;  // VSync: guaranteed available, no tearing
        }
        // Uncapped: prefer IMMEDIATE, fallback MAILBOX, then FIFO
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return availablePresentMode;
            }
        }
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanContext::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        int width, height;
        glfwGetFramebufferSize(m_WindowHandle, &width, &height);
        return { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    }

    void VulkanContext::CreateSwapChain() {
        SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_PhysicalDevice);

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.Formats);
        VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.PresentModes);
        VkExtent2D extent = ChooseSwapExtent(swapChainSupport.Capabilities);

        uint32_t imageCount = swapChainSupport.Capabilities.minImageCount + 1;
        if (swapChainSupport.Capabilities.maxImageCount > 0 && imageCount > swapChainSupport.Capabilities.maxImageCount) {
            imageCount = swapChainSupport.Capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_Surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice);
        uint32_t queueFamilyIndices[] = { indices.GraphicsFamily.value(), indices.PresentFamily.value() };

        if (indices.GraphicsFamily != indices.PresentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = swapChainSupport.Capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        VkResult result = vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_SwapChain);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create SwapChain!");

        vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, nullptr);
        m_SwapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, m_SwapChainImages.data());

        m_SwapChainImageFormat = surfaceFormat.format;
        m_SwapChainExtent = extent;
    }

    void VulkanContext::CreateImageViews() {
        m_SwapChainImageViews.resize(m_SwapChainImages.size());

        for (size_t i = 0; i < m_SwapChainImages.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_SwapChainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_SwapChainImageFormat;

            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            VkResult result = vkCreateImageView(m_Device, &createInfo, nullptr, &m_SwapChainImageViews[i]);
            AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Image Views!");
        }
    }

    void VulkanContext::CreateCommandPool() {
        QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_PhysicalDevice);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.GraphicsFamily.value();

        VkResult result = vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Command Pool!");

        VkCommandPoolCreateInfo oneTimePoolInfo{};
        oneTimePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        oneTimePoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        oneTimePoolInfo.queueFamilyIndex = queueFamilyIndices.GraphicsFamily.value();
        result = vkCreateCommandPool(m_Device, &oneTimePoolInfo, nullptr, &m_OneTimeCommandPool);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create One-Time Command Pool!");
    }

    void VulkanContext::CreateSyncObjects() {
        // Per-swapchain-image semaphores — avoids stale associations after swapchain recreation.
        uint32_t imageCount = (uint32_t)m_SwapChainImages.size();
        uint32_t oldSemCount = (uint32_t)m_ImageAvailableSemaphores.size();

        // Destroy any excess semaphores if swapchain image count shrunk
        for (uint32_t i = imageCount; i < oldSemCount; i++) {
            if (m_ImageAvailableSemaphores[i]) vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
            if (m_RenderFinishedSemaphores[i])  vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
        }

        m_ImageAvailableSemaphores.resize(imageCount, VK_NULL_HANDLE);
        m_RenderFinishedSemaphores.resize(imageCount, VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        // Create only the new slots (existing ones remain valid)
        for (uint32_t i = oldSemCount; i < imageCount; i++) {
            if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS) {
                AYAYA_CORE_ERROR("Failed to create semaphore for swapchain image {}", i);
            }
        }

        // Fences: per-frame-in-flight (3), indexed by m_CurrentFrame
        if (m_InFlightFences.empty()) {
            m_InFlightFences.resize(m_FramesInFlight);
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            for (uint32_t i = 0; i < m_FramesInFlight; i++) {
                vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFences[i]);
            }
        }
    }

    void VulkanContext::AllocateCommandBuffers() {
        m_CommandBuffers.resize(m_FramesInFlight);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

        VkResult result = vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data());
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate Command Buffers!");
    }

    void VulkanContext::CreateDescriptorPool() {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        VkResult result = vkCreateDescriptorPool(m_Device, &pool_info, nullptr, &m_DescriptorPool);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Descriptor Pool!");
    }

    static const char* VkResultStr(VkResult r) {
        switch (r) {
            case VK_SUCCESS: return "VK_SUCCESS";
            case VK_NOT_READY: return "VK_NOT_READY";
            case VK_TIMEOUT: return "VK_TIMEOUT";
            case VK_EVENT_SET: return "VK_EVENT_SET";
            case VK_EVENT_RESET: return "VK_EVENT_RESET";
            case VK_INCOMPLETE: return "VK_INCOMPLETE";
            case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
            case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
            case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
            case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
            case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
            case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
            case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
            case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
            case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
            case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
            case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
            case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
            default: return "UNKNOWN";
        }
    }

    void VulkanContext::BeginFrame() {
        vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

        // Process deferred bindless index releases now that the fence guarantees
        // the GPU has finished with this frame's resources.
        ProcessDeferredBindlessReleases();

        // Deferred swapchain rebuild (e.g., VSync toggle mid-frame).
        // Safe point: GPU is idle (fence waited), no command buffer recording active.
        if (m_VSyncPending) {
            m_VSyncPending = false;
            RecreateSwapChain();
        }

        VkResult result = vkAcquireNextImageKHR(m_Device, m_SwapChain, UINT64_MAX, m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &m_ImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            AYAYA_CORE_WARN("[DBG] BeginFrame: swapchain OUT_OF_DATE, recreating...");
            RecreateSwapChain();
            result = vkAcquireNextImageKHR(m_Device, m_SwapChain, UINT64_MAX, m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &m_ImageIndex);
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            AYAYA_CORE_ERROR("[DBG] BeginFrame: vkAcquireNextImageKHR FAILED! m_CurrentFrame={0}, m_ImageIndex={1}, result={2} ({3})",
                m_CurrentFrame, m_ImageIndex, (int)result, VkResultStr(result));
        }

        vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame]);

        VkResult resetResult = vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrame], 0);
        if (resetResult != VK_SUCCESS) {
            AYAYA_CORE_ERROR("[DBG] BeginFrame: vkResetCommandBuffer FAILED! m_CurrentFrame={0}, result={1} ({2})",
                m_CurrentFrame, (int)resetResult, VkResultStr(resetResult));
        }

        // Reset per-frame descriptor set indices after fence guarantees GPU is done
        VulkanPipeline::ResetAllDescriptorIndices();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VkResult beginResult = vkBeginCommandBuffer(m_CommandBuffers[m_CurrentFrame], &beginInfo);
        if (beginResult != VK_SUCCESS) {
            AYAYA_CORE_ERROR("[DBG] BeginFrame: vkBeginCommandBuffer FAILED! m_CurrentFrame={0}, result={1} ({2})",
                m_CurrentFrame, (int)beginResult, VkResultStr(beginResult));
        }

        // Reset ALL timestamp queries every frame.  vkCmdResetQueryPool is
        // cheap and guarantees every slot is fresh — avoids the first-frame
        // cold-start where m_TimestampSlotsUsed is still 0.
        if (m_TimestampPool != VK_NULL_HANDLE) {
            vkCmdResetQueryPool(m_CommandBuffers[m_CurrentFrame],
                m_TimestampPool, 0, kMaxTimestampQueries);
        }
        m_LastFrameSlotCount = m_TimestampSlotsUsed;
        m_TimestampSlotsUsed = 0;
    }

    uint32_t VulkanContext::AllocTimestampSlot() {
        if (m_TimestampPool == VK_NULL_HANDLE) return UINT32_MAX;
        if (m_TimestampSlotsUsed + 2 > kMaxTimestampQueries) return UINT32_MAX;
        uint32_t slot = m_TimestampSlotsUsed;
        m_TimestampSlotsUsed += 2;
        return slot;
    }

    void VulkanContext::ReadTimestampResults() {
        if (m_TimestampPool == VK_NULL_HANDLE || m_LastFrameSlotCount == 0) return;

        uint32_t count = m_LastFrameSlotCount;
        // With VK_QUERY_RESULT_WITH_AVAILABILITY_BIT, stride must be >= 16:
        // each query produces [result64, availability64] interleaved.
        const uint32_t stride = 16;
        uint32_t dataSize = count * stride;
        if (dataSize > m_TimestampResults.size() * sizeof(uint64_t)) {
            m_TimestampResults.resize(dataSize / sizeof(uint64_t));
        }

        // CRITICAL: Zero ALL availability bits in the buffer BEFORE reading.
        // Without this, stale avail=1 bits from a previous successful read leak
        // through when vkGetQueryPoolResults returns VK_NOT_READY. After zeroing,
        // only genuinely available GPU queries will have their avail bits set by
        // the driver.  (odd indices = availability slots in the interleaved layout)
        for (size_t i = 1; i < m_TimestampResults.size(); i += 2) {
            m_TimestampResults[i] = 0;
        }

        VkResult result = vkGetQueryPoolResults(m_Device, m_TimestampPool, 0, count,
            dataSize, m_TimestampResults.data(), stride,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

        if (result == VK_NOT_READY) {
            // DO NOT early-return. Partial results for available queries have
            // been written to the buffer (with avail=1). Queries that were not
            // ready retain avail=0 from our pre-zeroing above. Per-pass consumer
            // code checks availability bits as gate.
            m_TimestampNotReadyCount++;
            if (m_TimestampNotReadyCount % 60 == 1) {
                // AYAYA_CORE_WARN("[DBG] ReadTimestampResults: VK_NOT_READY x{} — "
                //     "partial results kept, consumers use avail bits as gate",
                //     m_TimestampNotReadyCount);
            }
        } else if (result == VK_SUCCESS) {
            m_TimestampNotReadyCount = 0;
        } else {
            // Unexpected error — zero the entire buffer so consumers get nothing
            AYAYA_CORE_ERROR("[DBG] ReadTimestampResults: vkGetQueryPoolResults "
                "FAILED! result={} ({})", (int)result, VkResultStr(result));
            std::fill(m_TimestampResults.begin(), m_TimestampResults.end(), 0);
        }
        // Results layout: [ts0, avail0, ts1, avail1, ...]
    }

    void VulkanContext::SwapBuffers() {
        VkResult endResult = vkEndCommandBuffer(m_CommandBuffers[m_CurrentFrame]);
        if (endResult != VK_SUCCESS) {
            AYAYA_CORE_ERROR("[DBG] SwapBuffers: vkEndCommandBuffer FAILED! m_CurrentFrame={0}, result={1} ({2})",
                m_CurrentFrame, (int)endResult, VkResultStr(endResult));
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphores[m_CurrentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffers[m_CurrentFrame];

        VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphores[m_ImageIndex] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        VkResult submitResult = vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_CurrentFrame]);
        if (submitResult != VK_SUCCESS) {
            AYAYA_CORE_ERROR("[DBG] SwapBuffers: vkQueueSubmit FAILED! m_CurrentFrame={0}, m_ImageIndex={1}, result={2} ({3})",
                m_CurrentFrame, m_ImageIndex, (int)submitResult, VkResultStr(submitResult));
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapChains[] = { m_SwapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &m_ImageIndex;

        VkResult result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            AYAYA_CORE_ERROR("[DBG] SwapBuffers: vkQueuePresentKHR FAILED! m_CurrentFrame={0}, m_ImageIndex={1}, result={2} ({3})",
                m_CurrentFrame, m_ImageIndex, (int)result, VkResultStr(result));
        }

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            AYAYA_CORE_WARN("[DBG] SwapBuffers: swapchain OUT_OF_DATE, recreating...");
            RecreateSwapChain();
        }

        m_CurrentFrame = (m_CurrentFrame + 1) % m_FramesInFlight;
    }

    VkCommandBuffer VulkanContext::BeginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_OneTimeCommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void VulkanContext::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkResult submitResult = vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        if (submitResult != VK_SUCCESS) {
            AYAYA_CORE_ERROR("[DBG] EndSingleTimeCommands: vkQueueSubmit FAILED! result={0} ({1})",
                (int)submitResult, VkResultStr(submitResult));
        }

        vkQueueWaitIdle(m_GraphicsQueue);
        vkDeviceWaitIdle(m_Device);

        vkResetCommandPool(m_Device, m_OneTimeCommandPool, 0);
    }

    void VulkanContext::CleanupSwapChain() {
        for (auto imageView : m_SwapChainImageViews) {
            vkDestroyImageView(m_Device, imageView, nullptr);
        }
        if (m_SwapChain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
            m_SwapChain = VK_NULL_HANDLE;
        }
        // NOTE: Do NOT destroy semaphores/fences here.
        // The presentation engine may still hold references to semaphores
        // from the old swapchain. They are resized (if needed) in CreateSyncObjects
        // after the new swapchain is built. vkDeviceWaitIdle drains GPU queues,
        // but not the presentation engine.
    }

    void VulkanContext::RecreateSwapChain() {
        AYAYA_CORE_WARN("[DBG] RecreateSwapChain called! m_CurrentFrame={0}", m_CurrentFrame);
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_WindowHandle, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(m_WindowHandle, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(m_Device);

        CleanupSwapChain();

        CreateSwapChain();
        CreateImageViews();
        CreateSyncObjects();   // fresh semaphores + fences for the new swapchain
        m_CurrentFrame = 0;    // reset frame ring-buffer index
        // 动态渲染：不再需要 CreateFramebuffers
    }

    void VulkanContext::SetVSync(bool vsync) {
        if (m_VSync == vsync) return;
        m_VSync = vsync;

        // Defer swapchain rebuild to BeginFrame() — cannot destroy swapchain
        // mid-frame (e.g., during ImGui rendering) or GPU will crash.
        if (m_SwapChain != VK_NULL_HANDLE) m_VSyncPending = true;
    }

    uint32_t VulkanContext::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        AYAYA_CORE_ASSERT(false, "Failed to find suitable memory type!");
        return 0;
    }

    VkFormat VulkanContext::FindDepthFormat() {
        std::vector<VkFormat> candidates = { VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
        for (VkFormat format : candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                return format;
            }
        }
        AYAYA_CORE_ASSERT(false, "Failed to find supported depth format!");
        return VK_FORMAT_UNDEFINED;
    }

    // ── Helper: create a 1×1 RGBA8 texture via staging buffer ──
    static void CreateDefaultTexture1x1(
        VkDevice device, VmaAllocator allocator,
        VkQueue graphicsQueue, VkCommandPool oneTimePool,
        const uint8_t pixels[4],
        VkImage& outImage, VkImageView& outView, VkSampler& outSampler,
        VmaAllocation& outAllocation)
    {
        // Staging buffer
        VkBufferCreateInfo stagingInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        stagingInfo.size = 4;  // 1×1 RGBA8
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                 VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer stagingBuffer;
        VmaAllocation stagingAlloc;
        vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                        &stagingBuffer, &stagingAlloc, nullptr);

        void* data;
        vmaMapMemory(allocator, stagingAlloc, &data);
        memcpy(data, pixels, 4);
        vmaUnmapMemory(allocator, stagingAlloc);

        // Destination image
        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { 1, 1, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        vmaCreateImage(allocator, &imageInfo, &allocCreateInfo,
                       &outImage, &outAllocation, nullptr);

        // One-time command buffer for layout transition + copy
        VkCommandBufferAllocateInfo cmdAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandPool = oneTimePool;
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // UNDEFINED → TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier barrier0{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier0.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier0.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier0.image = outImage;
        barrier0.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier0.srcAccessMask = 0;
        barrier0.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier0);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.imageExtent = { 1, 1, 1 };
        vkCmdCopyBufferToImage(cmd, stagingBuffer, outImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
        VkImageMemoryBarrier barrier1{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.image = outImage;
        barrier1.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier1.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier1);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, oneTimePool, 1, &cmd);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

        // Image view
        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = outImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(device, &viewInfo, nullptr, &outView);

        // Sampler
        VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        vkCreateSampler(device, &samplerInfo, nullptr, &outSampler);
    }

    void VulkanContext::CreateDefaultBindlessTextures() {
        const uint8_t whitePx[4]  = { 255, 255, 255, 255 };
        const uint8_t blackPx[4]  = { 0,   0,   0,   255 };
        const uint8_t normalPx[4] = { 128, 128, 255, 255 };  // tangent-space Z-up

        CreateDefaultTexture1x1(m_Device, m_Allocator, m_GraphicsQueue,
                                m_OneTimeCommandPool, whitePx,
                                m_DefaultWhiteImage, m_DefaultWhiteView,
                                m_DefaultWhiteSampler, m_DefaultWhiteAllocation);
        m_BindlessManager.UpdateBinding(m_Device,
            VulkanBindlessManager::kWhiteIndex,
            m_DefaultWhiteView, m_DefaultWhiteSampler);

        CreateDefaultTexture1x1(m_Device, m_Allocator, m_GraphicsQueue,
                                m_OneTimeCommandPool, blackPx,
                                m_DefaultBlackImage, m_DefaultBlackView,
                                m_DefaultBlackSampler, m_DefaultBlackAllocation);
        m_BindlessManager.UpdateBinding(m_Device,
            VulkanBindlessManager::kBlackIndex,
            m_DefaultBlackView, m_DefaultBlackSampler);

        CreateDefaultTexture1x1(m_Device, m_Allocator, m_GraphicsQueue,
                                m_OneTimeCommandPool, normalPx,
                                m_DefaultNormalImage, m_DefaultNormalView,
                                m_DefaultNormalSampler, m_DefaultNormalAllocation);
        m_BindlessManager.UpdateBinding(m_Device,
            VulkanBindlessManager::kDefaultNormalIndex,
            m_DefaultNormalView, m_DefaultNormalSampler);

        AYAYA_CORE_INFO("Default bindless textures registered at indices 1-3");
    }

    void VulkanContext::DestroyDefaultBindlessTextures() {
        auto destroyTex = [this](VkImage& img, VkImageView& view, VkSampler& samp,
                                  VmaAllocation& alloc) {
            if (samp)  { vkDestroySampler(m_Device, samp, nullptr); samp = VK_NULL_HANDLE; }
            if (view)  { vkDestroyImageView(m_Device, view, nullptr); view = VK_NULL_HANDLE; }
            if (alloc) { vmaDestroyImage(m_Allocator, img, alloc); img = VK_NULL_HANDLE; alloc = VK_NULL_HANDLE; }
        };

        destroyTex(m_DefaultWhiteImage,  m_DefaultWhiteView,  m_DefaultWhiteSampler,  m_DefaultWhiteAllocation);
        destroyTex(m_DefaultBlackImage,  m_DefaultBlackView,  m_DefaultBlackSampler,  m_DefaultBlackAllocation);
        destroyTex(m_DefaultNormalImage, m_DefaultNormalView, m_DefaultNormalSampler, m_DefaultNormalAllocation);
    }

    void VulkanContext::QueueDeferredBindlessRelease(uint32_t index) {
        if (index >= VulkanBindlessManager::kFirstFreeIndex) {
            m_DeferredBindlessReleases.push_back({ index, 3 });
        }
    }

    void VulkanContext::ProcessDeferredBindlessReleases() {
        for (auto it = m_DeferredBindlessReleases.begin();
             it != m_DeferredBindlessReleases.end(); ) {
            if (--it->FramesRemaining == 0) {
                m_BindlessManager.FreeIndex(it->Index);
                it = m_DeferredBindlessReleases.erase(it);
            } else {
                ++it;
            }
        }
    }
}
