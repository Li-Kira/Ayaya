#pragma once
#include "Renderer/Shader.hpp"
#include <vulkan/vulkan.h>

namespace Ayaya {

    class VulkanShader : public Shader {
    public:
        // 匹配你的工厂方法签名
        VulkanShader(const std::string& vertexPath, const std::string& fragmentPath);
        VulkanShader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);
        virtual ~VulkanShader() override;

        virtual void Bind() const override {}
        virtual void Unbind() const override {}

        virtual const std::string& GetName() const override { return m_Name; }

        // 提供给 VulkanPipeline 组装管线使用的接口
        VkShaderModule GetVertexShaderModule() const { return m_VertexShaderModule; }
        VkShaderModule GetFragmentShaderModule() const { return m_FragmentShaderModule; }

        // Returns true if both shader modules loaded successfully
        bool IsValid() const { return m_VertexShaderModule != VK_NULL_HANDLE
                                   && m_FragmentShaderModule != VK_NULL_HANDLE; }

        // Vulkan 不使用传统的 glUniform 设值，而是使用 PushConstants 或 DescriptorSets。
        // 为了防止抽象类报错，我们先架空这些旧的 OpenGL 接口。
        virtual void SetBool(const std::string& name, bool value) override {}
        virtual void SetInt(const std::string& name, int value) override {}
        virtual void SetIntArray(const std::string& name, int* values, uint32_t count) override {}
        virtual void SetFloat(const std::string& name, float value) override {}
        virtual void SetFloat2(const std::string& name, const glm::vec2& value) override {}
        virtual void SetFloat3(const std::string& name, const glm::vec3& value) override {}
        virtual void SetFloat4(const std::string& name, const glm::vec4& value) override {}
        virtual void SetMat2(const std::string& name, const glm::mat2& matrix) override {}
        virtual void SetMat3(const std::string& name, const glm::mat3& matrix) override {}
        virtual void SetMat4(const std::string& name, const glm::mat4& matrix) override {}
        virtual void BindUniformBlock(const std::string& name, uint32_t bindingPoint) override {}

    private:
        std::string m_Name;
        
        // 真正的 Vulkan 着色器模块
        VkShaderModule m_VertexShaderModule = VK_NULL_HANDLE;
        VkShaderModule m_FragmentShaderModule = VK_NULL_HANDLE;

        // 辅助读取和创建函数
        std::vector<char> ReadFile(const std::string& filepath);
        VkShaderModule CreateShaderModule(const std::vector<char>& code);
    };

}