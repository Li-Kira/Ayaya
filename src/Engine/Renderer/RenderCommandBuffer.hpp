#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "Renderer/VertexArray.hpp"
#include "Renderer/Pipeline.hpp"
#include "Renderer/Framebuffer.hpp"

namespace Ayaya {
    class Mesh;

    class RenderCommandBuffer {
    public:
        virtual ~RenderCommandBuffer() = default;

        virtual void Begin() = 0;
        virtual void End() = 0;

        // --- 基础状态控制 (过渡期保留) ---
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void SetDepthTest(bool enable) = 0;
        virtual void SetBlend(bool enable) = 0;
        virtual void SetDepthWrite(bool enable) = 0;          
        virtual void SetDepthFuncLEqual() = 0;                
        virtual void SetDepthFuncLess() = 0;                  
        virtual void SetBlendFuncAlpha() = 0;                 
        virtual void SetCullFaceFront() = 0; 
        virtual void SetCullFaceBack() = 0;  
        virtual void SetCullFace(bool enable) = 0;            
        virtual void SetPolygonModeLine() = 0;                
        virtual void SetPolygonModeFill() = 0;                
        virtual void SetLineWidth(float width) = 0;           

        virtual void SetClearColor(const glm::vec4& color) = 0;
        virtual void Clear() = 0;
        virtual void BlitDepth(uint32_t readFBO, uint32_t drawFBO, uint32_t width, uint32_t height) = 0;

        // ==========================================
        // 现代 API 核心语义
        // ==========================================
        virtual void BeginRenderPass(const std::shared_ptr<Framebuffer>& targetFBO, bool clear = true, const glm::vec4& clearColor = glm::vec4(0.0f)) = 0;
        virtual void EndRenderPass() = 0;
        virtual void BindPipeline(const std::shared_ptr<Pipeline>& pipeline) = 0;

        // --- 推送常量 ---
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, float data) = 0;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, int data) = 0;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec2& data) = 0;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec3& data) = 0;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec4& data) = 0;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::mat3& data) = 0;
        virtual void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::mat4& data) = 0;

        // --- 运行时描述符绑定 ---
        virtual void BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, uint32_t rendererID) = 0;
        virtual void BindTextureCube(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, uint32_t rendererID) = 0;

        // --- 绘制指令 ---
        virtual void DrawArrays(uint32_t vertexCount) = 0;
        virtual void DrawIndexed(uint32_t indexCount) = 0;
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
        virtual void DrawArrays(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) = 0;
        virtual void DrawTriangleStrip(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) = 0;

        // 【工厂方法】：负责根据 API 派发具体的底层对象！
        static std::shared_ptr<RenderCommandBuffer> Create();

        // 【新增】：现代 RHI 接口，直接接受纯数据资产 Mesh
        virtual void DrawIndexed(const std::shared_ptr<Mesh>& mesh, uint32_t indexCount = 0) = 0;
        virtual void DrawArrays(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount = 0) = 0;
        virtual void DrawTriangleStrip(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount = 0) = 0;
    };

}