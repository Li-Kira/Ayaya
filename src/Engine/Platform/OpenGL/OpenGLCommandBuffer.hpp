#pragma once
#include "Renderer/RenderCommandBuffer.hpp"

namespace Ayaya {
    class OpenGLCommandBuffer : public RenderCommandBuffer {
    public:
        OpenGLCommandBuffer() = default;
        virtual ~OpenGLCommandBuffer() = default;

        virtual void Begin() override {}
        virtual void End() override {}

        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        virtual void SetDepthTest(bool enable) override;
        virtual void SetBlend(bool enable) override;
        virtual void SetDepthWrite(bool enable) override;          
        virtual void SetDepthFuncLEqual() override;                
        virtual void SetDepthFuncLess() override;                  
        virtual void SetBlendFuncAlpha() override;                 
        virtual void SetCullFaceFront() override; 
        virtual void SetCullFaceBack() override;  
        virtual void SetCullFace(bool enable) override;            
        virtual void SetPolygonModeLine() override;                
        virtual void SetPolygonModeFill() override;                
        virtual void SetLineWidth(float width) override;           

        virtual void SetClearColor(const glm::vec4& color) override;
        virtual void Clear() override;
        virtual void BlitDepth(uint32_t readFBO, uint32_t drawFBO, uint32_t width, uint32_t height) override;

        virtual void BeginRenderPass(const std::shared_ptr<Framebuffer>& targetFBO, bool clear, const glm::vec4& clearColor) override;
        virtual void BeginRenderPass(const std::shared_ptr<Framebuffer>& targetFBO, bool clearColor, bool clearDepth, const glm::vec4& clearValue) override;
        virtual void BeginRenderPass(const std::shared_ptr<Framebuffer>& colorFBO, const std::shared_ptr<Framebuffer>& depthFBO, bool clearColor, bool clearDepth, const glm::vec4& clearValue) override;
        virtual void EndRenderPass() override;
        virtual void BindPipeline(const std::shared_ptr<Pipeline>& pipeline) override;

        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, float data) override;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, int data) override;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec2& data) override;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec3& data) override;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec4& data) override;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::mat3& data) override;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::mat4& data) override;
        virtual void PushConstantData(const std::shared_ptr<Pipeline>& pipeline, const void* data, uint32_t size) override;

        virtual void BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Texture2D>& texture) override;
        virtual void BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Framebuffer>& framebuffer, uint32_t attachmentIndex = 0, bool isDepth = false) override;

        // 【核心修改】
        virtual void BindTextureCube(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<TextureCube>& textureCube) override;

        virtual void DrawArrays(uint32_t vertexCount) override;
        virtual void DrawIndexed(uint32_t indexCount) override;
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount) override;
        virtual void DrawArrays(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) override;
        virtual void DrawTriangleStrip(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) override;
        // 【新增】：实现 Mesh 接口
        virtual void DrawIndexed(const std::shared_ptr<Mesh>& mesh, uint32_t indexCount = 0) override;
        virtual void DrawIndexedInstanced(const std::shared_ptr<Mesh>& mesh,
                                           uint32_t indexCount,
                                           uint32_t instanceCount,
                                           uint32_t firstInstance) override {}
        virtual void DrawArrays(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount = 0) override;
        virtual void DrawTriangleStrip(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount = 0) override;
        virtual void BlitDepth(const std::shared_ptr<Framebuffer>& readFBO, const std::shared_ptr<Framebuffer>& drawFBO, uint32_t width, uint32_t height) override;
        virtual void DrawTriangleStrip(uint32_t vertexCount) override;

        virtual void WriteTimestamp(uint32_t, bool) override {}
        virtual void InsertExecutionBarrier() override {}
        virtual void TransitionImageLayout(const std::shared_ptr<Framebuffer>&,
                                           uint32_t, ImageLayout, ImageLayout) override {}
        virtual void FlushDescriptorSets() override {}
        virtual void SetPerAttachmentClearColors(const std::vector<glm::vec4>&) override {}
    };
}