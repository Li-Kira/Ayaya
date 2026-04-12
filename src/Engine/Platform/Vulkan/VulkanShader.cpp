#include "ayapch.h"
#include "VulkanShader.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include <fstream>

namespace Ayaya {

    // ==========================================
    // 核心：二进制读取器 (游标移到末尾获取大小，一次性读入)
    // ==========================================
    std::vector<char> VulkanShader::ReadFile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            AYAYA_CORE_ERROR("VulkanShader: Failed to open SPIR-V file: {0}", filepath);
            return std::vector<char>();
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        return buffer;
    }

    VkShaderModule VulkanShader::CreateShaderModule(const std::vector<char>& code) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            AYAYA_CORE_ERROR("Failed to create Vulkan Shader Module!");
            return VK_NULL_HANDLE;
        }
        return shaderModule;
    }

    // ==========================================
    // 构造函数
    // ==========================================
    VulkanShader::VulkanShader(const std::string& vertexPath, const std::string& fragmentPath) {
        // 从路径里提取名字
        auto lastSlash = vertexPath.find_last_of("/\\");
        lastSlash = lastSlash == std::string::npos ? 0 : lastSlash + 1;
        auto lastDot = vertexPath.rfind('.');
        auto count = lastDot == std::string::npos ? vertexPath.size() - lastSlash : lastDot - lastSlash;
        m_Name = vertexPath.substr(lastSlash, count);

        std::vector<char> vertCode = ReadFile(vertexPath);
        std::vector<char> fragCode = ReadFile(fragmentPath);

        m_VertexShaderModule = CreateShaderModule(vertCode);
        m_FragmentShaderModule = CreateShaderModule(fragCode);

        if (m_VertexShaderModule && m_FragmentShaderModule) {
            AYAYA_CORE_INFO("VulkanShader '{0}' loaded successfully from SPIR-V!", m_Name);
        }
    }

    VulkanShader::VulkanShader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath)
        : m_Name(name) {
        std::vector<char> vertCode = ReadFile(vertexPath);
        std::vector<char> fragCode = ReadFile(fragmentPath);

        m_VertexShaderModule = CreateShaderModule(vertCode);
        m_FragmentShaderModule = CreateShaderModule(fragCode);
    }

    VulkanShader::~VulkanShader() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (context) {
            VkDevice device = context->GetDevice();
            if (m_VertexShaderModule) vkDestroyShaderModule(device, m_VertexShaderModule, nullptr);
            if (m_FragmentShaderModule) vkDestroyShaderModule(device, m_FragmentShaderModule, nullptr);
        }
    }

}