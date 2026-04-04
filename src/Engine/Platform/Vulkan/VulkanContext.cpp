#include "ayapch.h"
#include "VulkanContext.hpp"
#include "Core/Log.hpp"
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

namespace Ayaya {

    VulkanContext::VulkanContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle) {
        AYAYA_CORE_ASSERT(windowHandle, "Window handle is null!");
    }

    VulkanContext::~VulkanContext() {
        // 【新增】：优先销毁逻辑设备！
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

    void VulkanContext::SwapBuffers() {
        // 留空，稍后实现
    }

    void VulkanContext::CreateLogicalDevice() {
        QueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice);

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
}