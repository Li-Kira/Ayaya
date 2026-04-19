#pragma once
#include "Renderer/RenderCommandBuffer.hpp"
#include <vulkan/vulkan.h>
#include <unordered_map>

namespace Ayaya {
    class VulkanPipeline;

    class VulkanRenderCommandBuffer : public RenderCommandBuffer {
    public:
        VulkanRenderCommandBuffer();
        virtual ~VulkanRenderCommandBuffer() override;

        virtual void Begin() override;
        virtual void End() override;

        // --- 基础状态控制 (过渡期保留) ---
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override {}
        virtual void SetDepthTest(bool enable) override {}
        virtual void SetBlend(bool enable) override {}
        virtual void SetDepthWrite(bool enable) override {}          
        virtual void SetDepthFuncLEqual() override {}                
        virtual void SetDepthFuncLess() override {}                  
        virtual void SetBlendFuncAlpha() override {}                 
        virtual void SetCullFaceFront() override {} 
        virtual void SetCullFaceBack() override {}  
        virtual void SetCullFace(bool enable) override {}            
        virtual void SetPolygonModeLine() override {}                
        virtual void SetPolygonModeFill() override {}                
        virtual void SetLineWidth(float width) override {}           

        virtual void SetClearColor(const glm::vec4& color) override {}
        virtual void Clear() override {}
        virtual void BlitDepth(uint32_t readFBO, uint32_t drawFBO, uint32_t width, uint32_t height) override {}
        virtual void BlitDepth(const std::shared_ptr<Framebuffer>& readFBO, const std::shared_ptr<Framebuffer>& drawFBO, uint32_t width, uint32_t height) override {}

        // --- 现代 API 核心语义 ---
        virtual void BeginRenderPass(const std::shared_ptr<Framebuffer>& targetFBO, bool clear, const glm::vec4& clearColor) override;
        virtual void EndRenderPass() override;

        virtual void BindPipeline(const std::shared_ptr<Pipeline>& pipeline) override;

        // 【保留旧的散装 PushConstant，但给出警告】
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, float data) override {}
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, int data) override {}
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec2& data) override {}
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec3& data) override {}
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec4& data) override {}
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::mat3& data) override {}
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::mat4& data) override {}

        // 【核心】：推入连续结构体内存块
        virtual void PushConstantData(const std::shared_ptr<Pipeline>& pipeline, const void* data, uint32_t size) override;

        // --- 纹理绑定 ---
        virtual void BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Texture2D>& texture) override;
        virtual void BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Framebuffer>& framebuffer, uint32_t attachmentIndex = 0, bool isDepth = false) override;
        virtual void BindTextureCube(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<TextureCube>& textureCube) override;

        // --- 绘制指令 ---
        virtual void DrawArrays(uint32_t vertexCount) override;
        virtual void DrawIndexed(uint32_t indexCount) override {}
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount = 0) override {}
        virtual void DrawArrays(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) override;
        virtual void DrawTriangleStrip(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) override {}

        virtual void DrawIndexed(const std::shared_ptr<Mesh>& mesh, uint32_t indexCount = 0) override;
        virtual void DrawArrays(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount = 0) override;
        virtual void DrawTriangleStrip(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount = 0) override;
        virtual void DrawTriangleStrip(uint32_t vertexCount) override;

        // 管线屏障
        virtual void InsertExecutionBarrier() override;

    private:
        std::shared_ptr<VulkanPipeline> m_BoundPipeline; // 记住当前管线
        std::unordered_map<uint32_t, VkDescriptorImageInfo> m_PendingImageInfos; // 记住即将绑定的贴图

        void FlushDescriptorSets(); // 核心发车函数
    };

}