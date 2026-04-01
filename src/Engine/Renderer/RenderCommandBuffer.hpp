#pragma once
#include <cstdint>
#include <memory>
#include <glm/glm.hpp>
#include "Renderer/VertexArray.hpp" // 引入 VAO

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
        void DrawArrays(uint32_t vertexCount);
        void DrawIndexed(uint32_t indexCount);
        
        // --- 携带 VAO 的抽象绘制 ---
        void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount = 0);
        void DrawArrays(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount);
        void DrawTriangleStrip(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount);
    };

}