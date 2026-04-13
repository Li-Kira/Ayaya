#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    // ==========================================
    // 【核心对齐】：严格匹配 GLSL std430 规则
    // float 占 4 字节，而 vec2 强制要求 8 字节对齐
    // 因此在 Exposure 之后必须填充 4 字节的 padding！
    // ==========================================
    struct alignas(16) PostProcessPushConstants {
        float Exposure;         // 4 bytes (0-3)
        float _padding1;        // 4 bytes (4-7) 强制对齐位
        glm::vec2 TexelSize;    // 8 bytes (8-15)
        int ToneMappingType;    // 4 bytes (16-19)
        int EnableBloom;        // 4 bytes (20-23)
        float BloomIntensity;   // 4 bytes (24-27)
        float _padding2;        // 4 bytes (28-31) 补齐 16 字节的倍数
    };

    class VulkanPostProcessPass : public RenderPass {
    public:
        VulkanPostProcessPass();
        virtual ~VulkanPostProcessPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Shader> m_PostProcessShader;
        std::shared_ptr<Framebuffer> m_PostProcessFBO;
        
        std::shared_ptr<Pipeline> m_Pipeline; 
    };

}