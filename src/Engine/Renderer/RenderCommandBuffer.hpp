#pragma once
#include <cstdint>
#include <memory>
#include <glm/glm.hpp>
#include "Renderer/VertexArray.hpp" // 引入 VAO
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    class RenderCommandBuffer {
    public:
        RenderCommandBuffer() = default;
        ~RenderCommandBuffer() = default;

        void Begin();
        void End();

        // --- 基础状态控制 ---
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
        void SetDepthTest(bool enable);
        void SetBlend(bool enable);
        
        // --- 深度与混合控制 ---
        void SetDepthWrite(bool enable);          
        void SetDepthFuncLEqual();                
        void SetDepthFuncLess();                  
        void SetBlendFuncAlpha();                 
        
        // --- 面剔除与多边形模式 ---
        void SetCullFaceFront(); 
        void SetCullFaceBack();  
        void SetCullFace(bool enable);            
        void SetPolygonModeLine();                
        void SetPolygonModeFill();                
        void SetLineWidth(float width);           

        // --- 清屏与缓冲控制 ---
        void SetClearColor(const glm::vec4& color);
        void Clear();
        void BlitDepth(uint32_t readFBO, uint32_t drawFBO, uint32_t width, uint32_t height);

        // --- 资源绑定与绘制 ---
        void BindTexture2D(uint32_t slot, uint32_t rendererID);
        void BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& uniformName, uint32_t slot, uint32_t rendererID);
        void BindTextureCube(const std::shared_ptr<Pipeline>& pipeline, const std::string& uniformName, uint32_t slot, uint32_t rendererID);

        // ==========================================
        // 【新增】：现代 API 渲染作用域语义
        // ==========================================
        void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, float data);
        void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, int data);
        void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec2& data);
        void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec3& data);
        void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec4& data);
        void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::mat3& data);
        void PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::mat4& data);

        void DrawArrays(uint32_t vertexCount);
        void DrawIndexed(uint32_t indexCount);
        // --- 携带 VAO 的抽象绘制 ---
        void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount = 0);
        void DrawArrays(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount);
        void DrawTriangleStrip(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount);

        void BindPipeline(const std::shared_ptr<Pipeline>& pipeline);

        // ==========================================
        // 【新增】：现代 API 渲染作用域语义
        // ==========================================
        void BeginRenderPass(const std::shared_ptr<Framebuffer>& targetFBO, bool clear = true, const glm::vec4& clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        void EndRenderPass();
    };

}