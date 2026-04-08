#include "ayapch.h"
#include "VulkanContext.hpp"
#include "Core/Log.hpp"
#include <GLFW/glfw3.h>
#include <vector>

namespace Ayaya {

    VulkanContext::VulkanContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle) {
        AYAYA_CORE_ASSERT(windowHandle, "Window handle is null!");
    }

    VulkanContext::~VulkanContext() {
        // 等待 GPU 彻底空闲，防止在渲染途中强行销毁资源
        if (m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Device);
        }
        
        // 销毁描述符池
        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
            AYAYA_CORE_INFO("Vulkan Descriptor Pool destroyed.");
        }
        
        // 销毁同步对象
        for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); i++) {
            vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
            vkDestroyFence(m_Device, m_InFlightFences[i], nullptr);
        }
        
        // 销毁命令池
        if (m_CommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
            AYAYA_CORE_INFO("Vulkan Command Pool and Sync Objects destroyed.");
        }

        // ==========================================
        // 【核心修改】：直接调用清理函数，销毁画布、视图和帧缓冲
        // 这样可以避免代码重复，且保证重建和退出时的清理逻辑完全一致！
        // ==========================================
        CleanupSwapChain();

        // 销毁 RenderPass (它独立于 Swapchain 存在，所以保留在这里销毁)
        if (m_RenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
            AYAYA_CORE_INFO("Vulkan Render Pass destroyed.");
        }

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
        AYAYA_CORE_INFO("VulkanContext::Init - Initiating Vulkan Backend...");
        AYAYA_CORE_ASSERT(glfwVulkanSupported(), "GLFW must be compiled with Vulkan support!");

        // 1. 创建 Instance (保留你之前写的代码)
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Ayaya Editor";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Ayaya Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2; 

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

        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan Instance!");
        AYAYA_CORE_INFO("Vulkan Instance created successfully!");

        // ==========================================
        // 【新增】：2. 创建窗口表面 (Surface)
        // ==========================================
        CreateSurface();

        // ==========================================
        // 【新增】：3. 挑选物理显卡 (Physical Device)
        // ==========================================
        PickPhysicalDevice();

        // 【新增】：4. 创建逻辑设备与队列
        CreateLogicalDevice();

        // 【新增】：5. 创建交换链与图像视图
        CreateSwapChain();
        CreateImageViews();

        // 【新增】：6. 创建 Render Pass 与 Swapchain Framebuffers
        CreateRenderPass();
        CreateFramebuffers();

        // 【新增】：7. 创建命令池与同步原语
        CreateCommandPool();
        CreateSyncObjects();

        // 【新增】：8. 分配命令缓冲与创建描述符池
        AllocateCommandBuffers();
        CreateDescriptorPool();
    }

    // ------------------------------------------------------------------------
    // 以下是新增的 4 个辅助函数实现
    // ------------------------------------------------------------------------

    void VulkanContext::CreateSurface() {
        // 妙啊！GLFW 已经帮我们把繁琐的操作系统底层 (Win32/Cocoa/X11) 封装好了
        // 直接调用这一个函数，就能生成对应的 Vulkan Surface！
        VkResult result = glfwCreateWindowSurface(m_Instance, m_WindowHandle, nullptr, &m_Surface);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Window Surface!");
        AYAYA_CORE_INFO("Vulkan Window Surface created successfully!");
    }

    void VulkanContext::PickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
        
        AYAYA_CORE_ASSERT(deviceCount > 0, "Failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

        // 遍历电脑上的所有显卡，找到第一张符合我们要求的显卡
        for (const auto& device : devices) {
            if (IsDeviceSuitable(device)) {
                m_PhysicalDevice = device;
                break;
            }
        }

        AYAYA_CORE_ASSERT(m_PhysicalDevice != VK_NULL_HANDLE, "Failed to find a suitable GPU!");

        // 打印出我们选中的显卡名字，满足一下成就感！
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &deviceProperties);
        AYAYA_CORE_INFO("Selected GPU: {0}", deviceProperties.deviceName);
    }

    bool VulkanContext::IsDeviceSuitable(VkPhysicalDevice device) {
        // 只要这块显卡同时拥有“图形渲染”和“屏幕呈现”的队列族，我们就认为它合格了！
        // 在更高级的引擎里，这里还可以给显卡打分（比如独立显卡 +1000分，集成显卡 +100分，选分高的）
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
            // 寻找支持图形命令 (VK_QUEUE_GRAPHICS_BIT) 的队列族
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.GraphicsFamily = i;
            }

            // 寻找支持将画面呈现到我们刚建好的 Surface 上的队列族
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
            if (presentSupport) {
                indices.PresentFamily = i;
            }

            if (indices.IsComplete()) break;
            i++;
        }

        return indices;
    }

    void VulkanContext::CreateLogicalDevice() {
        QueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice);
        m_GraphicsQueueFamily = indices.GraphicsFamily.value();

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        // 使用 set 可以自动去重。如果 Graphics 和 Present 是同一个队列族，这里只会保留一个元素
        std::set<uint32_t> uniqueQueueFamilies = { indices.GraphicsFamily.value(), indices.PresentFamily.value() };

        float queuePriority = 1.0f; // 队列优先级：0.0 ~ 1.0，目前只有 1 个队列，直接给满分
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        // 指定我们需要的设备特性（比如几何着色器、各向异性过滤等，暂时留空）
        VkPhysicalDeviceFeatures deviceFeatures{};

        // ==========================================
        // 关键：启用 Swapchain (交换链) 扩展！
        // 这是渲染画面并显示到窗口上所必须的扩展。
        // ==========================================
        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        // Mac 兼容性：MoltenVK 必须开启 portability_subset 扩展
#ifdef __APPLE__
        deviceExtensions.push_back("VK_KHR_portability_subset");
#endif

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // 再次开启验证层，主要为了兼容老版本的 Vulkan 驱动
        const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        // 提交申请，向物理显卡索要我们的“指挥部”！
        VkResult result = vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create logical device!");

        // 从设备中获取真正用于提交命令的 Queue 句柄
        vkGetDeviceQueue(m_Device, indices.GraphicsFamily.value(), 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, indices.PresentFamily.value(), 0, &m_PresentQueue);

        AYAYA_CORE_INFO("Vulkan Logical Device & Queues created successfully!");
    }

    // =========================================================================
    // Swapchain 辅助与实现
    // =========================================================================

    SwapChainSupportDetails VulkanContext::QuerySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;

        // 1. 基础能力 (最小/最大图像数量，最小/最大宽高)
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.Capabilities);

        // 2. 支持的表面格式 (如 RGBA8 SRGB 等)
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.Formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.Formats.data());
        }

        // 3. 支持的呈现模式 (如 V-Sync, Mailbox 等)
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.PresentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, details.PresentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR VulkanContext::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        // 【核心修复 1】：将 B8G8R8A8_SRGB 改为 B8G8R8A8_UNORM！
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        // 退而求其次：随便挑第一个
        return availableFormats[0];
    }

    VkPresentModeKHR VulkanContext::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        // 首选：MAILBOX (也就是所谓的三重缓冲，延迟最低且不撕裂)
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }
        // 保底：FIFO (相当于传统的垂直同步 V-Sync，Vulkan 规定所有显卡必须支持它)
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanContext::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        int width, height;
        glfwGetFramebufferSize(m_WindowHandle, &width, &height);

        // 【核心修复】：彻底删掉 std::clamp！无视 Vulkan 提供的错误上限。
        // 强制使用 GLFW 探测到的绝对物理像素，夺回屏幕的完整控制权！
        return { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    }

    void VulkanContext::CreateSwapChain() {
        SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_PhysicalDevice);

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.Formats);
        VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.PresentModes);
        VkExtent2D extent = ChooseSwapExtent(swapChainSupport.Capabilities);

        // 决定弹夹里装多少张画布 (推荐：最小数量 + 1，防止我们在等显卡画图时没画布可用)
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
        createInfo.imageArrayLayers = 1; // 除非开发 VR 应用，否则都是 1
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // 声明我们将直接把颜色画到这上面

        QueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice);
        uint32_t queueFamilyIndices[] = { indices.GraphicsFamily.value(), indices.PresentFamily.value() };

        // 如果图形队列和呈现队列不是同一个部门，就得让它们跨部门共享画布
        if (indices.GraphicsFamily != indices.PresentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // 独占模式性能最好！
        }

        createInfo.preTransform = swapChainSupport.Capabilities.currentTransform; // 不做任何旋转等预变换
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // 忽略窗口的 Alpha 通道（不和操作系统其他窗口混合）
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE; // 遮挡剔除（如果窗口被其他窗口挡住，那部分就不画了，提升性能）
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        VkResult result = vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_SwapChain);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create SwapChain!");

        // 提取显卡实际为我们分配好的画布(Images)把手
        vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, nullptr);
        m_SwapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, m_SwapChainImages.data());

        m_SwapChainImageFormat = surfaceFormat.format;
        m_SwapChainExtent = extent;

        // 【修改】：让飞行帧数量完全等于 Swapchain 画布的数量 (Mac 上通常是 3)
        m_FramesInFlight = imageCount;

        AYAYA_CORE_INFO("Vulkan SwapChain created successfully! Images count: {0}", imageCount);
    }

    void VulkanContext::CreateImageViews() {
        // 在 Vulkan 中，我们不能直接对着 Image 画图。
        // Image 只是显存里的一块肉，我们必须给它套上 ImageView (视图)，告诉显卡：“把它当成一张二维的 RGBA 贴图来看待”。
        m_SwapChainImageViews.resize(m_SwapChainImages.size());

        for (size_t i = 0; i < m_SwapChainImages.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_SwapChainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_SwapChainImageFormat;

            // 颜色通道的映射 (默认即可)
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            // 告诉视图它涵盖了图像的哪些部分 (这里是没有 Mipmap 的全彩色图)
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            VkResult result = vkCreateImageView(m_Device, &createInfo, nullptr, &m_SwapChainImageViews[i]);
            AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Image Views!");
        }

        AYAYA_CORE_INFO("Vulkan SwapChain Image Views created successfully!");
    }

    // =========================================================================
    // Render Pass & Framebuffers 实现
    // =========================================================================

    void VulkanContext::CreateRenderPass() {
        // 1. 颜色附件 (Color Attachment)：描述我们要画到哪种格式的画布上
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_SwapChainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // 主窗口不需要 MSAA，MSAA 我们留给内部的 G-Buffer
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 每次画之前，清空上一帧的画面
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // 画完之后，保存画面以便显示到屏幕上
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // 画之前，我们不在乎画布原来是什么布局
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // 画完之后，画布必须转换为“可以呈现给屏幕”的布局！

        // 2. 颜色附件的引用：用于在 Subpass 中指向上面的描述
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0; // 对应上面 colorAttachment 的索引
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // 3. 子通道 (Subpass)：Vulkan 允许一个 RenderPass 包含多个步骤，目前我们只需要 1 个图形子通道
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        // 4. 子通道依赖 (Subpass Dependency)：解决显卡执行顺序和图像布局转换的同步问题
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // 隐式的外部 Subpass (上一次向屏幕呈现的操作)
        dependency.dstSubpass = 0; // 我们的 subpass 索引
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // 5. 正式创建 Render Pass
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        VkResult result = vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Render Pass!");
        AYAYA_CORE_INFO("Vulkan Render Pass created successfully!");
    }

    void VulkanContext::CreateFramebuffers() {
        // 交换链里有几张画布（ImageView），我们就得建多少个 Framebuffer
        m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size());

        for (size_t i = 0; i < m_SwapChainImageViews.size(); i++) {
            VkImageView attachments[] = {
                m_SwapChainImageViews[i] // 将当前画布绑定到刚才 RenderPass 里的 attachment = 0
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_RenderPass; // 遵守这个 RenderPass 的规矩
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = m_SwapChainExtent.width;
            framebufferInfo.height = m_SwapChainExtent.height;
            framebufferInfo.layers = 1;

            VkResult result = vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_SwapChainFramebuffers[i]);
            AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Framebuffer!");
        }

        AYAYA_CORE_INFO("Vulkan SwapChain Framebuffers created successfully!");
    }

    // =========================================================================
    // Command Pool & Sync Objects 实现
    // =========================================================================

    void VulkanContext::CreateCommandPool() {
        // 命令池是和具体的“队列部门”绑定的。我们要向图形队列提交画图指令，所以选用 GraphicsFamily。
        QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_PhysicalDevice);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        
        // 关键标志位：允许我们在每一帧单独重置(清空) Command Buffer，而不是一竿子打死重置整个池子
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; 
        poolInfo.queueFamilyIndex = queueFamilyIndices.GraphicsFamily.value();

        VkResult result = vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Command Pool!");
        AYAYA_CORE_INFO("Vulkan Command Pool created successfully!");
    }

    void VulkanContext::CreateSyncObjects() {
        m_ImageAvailableSemaphores.resize(m_FramesInFlight);
        m_RenderFinishedSemaphores.resize(m_FramesInFlight);
        m_InFlightFences.resize(m_FramesInFlight);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        // 【核弹级细节】：我们在创建 Fence 的时候，默认将其设置为 "Signaled (已放行)" 状态。
        // 为什么？因为在渲染第一帧时，CPU 会一上来就等 Fence。如果默认为未放行，CPU 就会永远卡死在第一帧！
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; 

        for (size_t i = 0; i < m_FramesInFlight; i++) {
            if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS) {
                
                AYAYA_CORE_ERROR("Failed to create synchronization objects for a frame!");
            }
        }

        AYAYA_CORE_INFO("Vulkan Synchronization Objects created successfully!");
    }

    // =========================================================================
    // Command Buffers & Descriptor Pool 实现
    // =========================================================================

    void VulkanContext::AllocateCommandBuffers() {
        m_CommandBuffers.resize(m_FramesInFlight);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        // PRIMARY 级别的缓冲可以直接提交给队列执行
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; 
        allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

        VkResult result = vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data());
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate Command Buffers!");
        AYAYA_CORE_INFO("Vulkan Command Buffers allocated successfully!");
    }

    void VulkanContext::CreateDescriptorPool() {
        // ImGui 需要极其庞大的描述符池来支撑它那复杂的 UI 渲染（比如每画一张图片都需要一个描述符）
        // 这是 ImGui 官方推荐的 Vulkan 资源池配置比例：
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
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes); // 允许分配的最大集合数
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        VkResult result = vkCreateDescriptorPool(m_Device, &pool_info, nullptr, &m_DescriptorPool);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Descriptor Pool!");
        AYAYA_CORE_INFO("Vulkan Descriptor Pool created successfully!");
    }

    // =========================================================================
    // Render Loop & Commands 实现
    // =========================================================================

    void VulkanContext::BeginFrame() {
        // 1. 等待上一帧 (CPU 不能跑得比 GPU 快太多)
        vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

        // 2. 向 Swapchain 索要一张可用的画布
        vkAcquireNextImageKHR(m_Device, m_SwapChain, UINT64_MAX, m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &m_ImageIndex);

        // 3. 放行围栏，准备录制新指令
        vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame]);
        vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrame], 0);

        // 4. 开启命令缓冲
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(m_CommandBuffers[m_CurrentFrame], &beginInfo);

        // 5. 开启 Render Pass (告诉显卡我们要往这张图上画了！)
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_RenderPass;
        renderPassInfo.framebuffer = m_SwapChainFramebuffers[m_ImageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_SwapChainExtent;

        // 这里的清理颜色就是 ImGui UI 背后的纯色底板
        VkClearValue clearColor = {{{0.08f, 0.085f, 0.09f, 1.0f}}};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(m_CommandBuffers[m_CurrentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void VulkanContext::SwapBuffers() {
        // 1. 结束 Render Pass 和命令录制
        vkCmdEndRenderPass(m_CommandBuffers[m_CurrentFrame]);
        vkEndCommandBuffer(m_CommandBuffers[m_CurrentFrame]);

        // 2. 将写好的“待办清单”提交给图形队列
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphores[m_CurrentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffers[m_CurrentFrame];

        VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphores[m_CurrentFrame] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_CurrentFrame]);

        // 3. 把画好的图推送到屏幕上
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapChains[] = { m_SwapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &m_ImageIndex;

        vkQueuePresentKHR(m_PresentQueue, &presentInfo);

        // 4. 推进到下一帧
        m_CurrentFrame = (m_CurrentFrame + 1) % m_FramesInFlight;
    }

    VkCommandBuffer VulkanContext::BeginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_CommandPool;
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

        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        // 为了安全，强制等待传输完毕
        vkQueueWaitIdle(m_GraphicsQueue);

        vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
    }

    // 【新增】：清理旧的交换链及其关联资源
    void VulkanContext::CleanupSwapChain() {
        for (auto framebuffer : m_SwapChainFramebuffers) {
            vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
        }
        for (auto imageView : m_SwapChainImageViews) {
            vkDestroyImageView(m_Device, imageView, nullptr);
        }
        if (m_SwapChain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
        }
    }

    // 【新增】：重新侦测屏幕尺寸并重建画布
    void VulkanContext::RecreateSwapChain() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_WindowHandle, &width, &height);
        // 如果窗口被最小化了，就暂停程序等待恢复
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(m_WindowHandle, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(m_Device); // 等待 GPU 停下手头工作

        CleanupSwapChain(); // 砸碎旧的

        // 创建匹配新分辨率的资源！
        CreateSwapChain();
        CreateImageViews();
        CreateFramebuffers();
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
}

