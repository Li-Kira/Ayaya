#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "Renderer/VertexArray.hpp"
#include "Renderer/Pipeline.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/Texture.hpp"

namespace Ayaya {
    class Mesh;
    class TextureCube;

    // ==========================================
    // 跨平台图像布局抽象 (RHI 隔离层)
    // ==========================================
    enum class ImageLayout {
        Undefined = 0,
        ColorAttachmentOptimal,
        DepthStencilAttachmentOptimal,
        ShaderReadOnlyOptimal,
        DepthStencilReadOnlyOptimal,  // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        TransferSrcOptimal,
        TransferDstOptimal,
        PresentSrc
    };

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
        virtual void BlitDepth(const std::shared_ptr<Framebuffer>& readFBO, const std::shared_ptr<Framebuffer>& drawFBO, uint32_t width, uint32_t height) = 0;

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
        // 【核心新增】：现代 Vulkan 架构的泛型 PushConstant 接口，直接推送一整块内存结构体
        virtual void PushConstantData(const std::shared_ptr<Pipeline>& pipeline, const void* data, uint32_t size) = 0;

        // ==========================================
        // --- 运行时描述符绑定 (现代多态接口) ---
        // ==========================================
        // 1. 绑定独立的 2D 贴图对象
        virtual void BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Texture2D>& texture) = 0;
        
        // 2. 绑定离线画布 (Framebuffer) 上的某个附件作为贴图
        virtual void BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Framebuffer>& framebuffer, uint32_t attachmentIndex = 0, bool isDepth = false) = 0;
        virtual void BindTextureCube(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<TextureCube>& textureCube) = 0;


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
        virtual void DrawTriangleStrip(uint32_t vertexCount) = 0;

        // 插入全局执行屏障，用于隔离前后两个具有依赖关系的 Pass
        virtual void InsertExecutionBarrier() = 0;

        // 精确的 Image Layout 转换屏障 (替代全局 Barrier)
        virtual void TransitionImageLayout(const std::shared_ptr<Framebuffer>& fbo,
                                           uint32_t attachmentIndex,
                                           ImageLayout oldLayout,
                                           ImageLayout newLayout) = 0;

        // Flush 延迟的 descriptor set 写入 (Vulkan 专用, OpenGL 空实现)
        virtual void FlushDescriptorSets() = 0;
    };

}