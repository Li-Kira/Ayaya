#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    struct alignas(16) TAAPushConstants {
        glm::mat4 InvViewProjection;   // current frame (camera-only motion)
        glm::mat4 PrevViewProjection;  // previous frame
        glm::vec2 Jitter;              // current frame sub-pixel jitter (UV units)
        glm::vec2 TexelSize;           // 1/w, 1/h
        float BlendFactor;             // base current-frame weight
        float DepthThreshold;          // disocclusion depth diff threshold
        float NormalThreshold;         // disocclusion normal dot threshold
        float MotionThreshold;         // large-motion history rejection gate
        float SharpenAmount;           // adaptive sharpen strength
        float Enable;                  // passthrough when 0
    };

    class VulkanTAAPass : public RenderPass {
    public:
        VulkanTAAPass();
        virtual ~VulkanTAAPass() override = default;

        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        std::shared_ptr<Shader>      m_Shader;
        std::shared_ptr<Pipeline>    m_Pipeline;
        std::shared_ptr<Framebuffer> m_RefFBO;

        // Cross-frame history (NOT RenderGraph managed) — the RenderGraph triple-buffers
        // "TAA_Output", so storing its FBO pointer gives previous-frame persistence for
        // free (frame N reads slot (N-1)%3, which still holds frame N-1's data).
        std::shared_ptr<Framebuffer> m_HistoryFBO[3];       // RGBA16F, full-res
        std::shared_ptr<Framebuffer> m_DepthHistoryFBO[3];  // previous-frame SceneDepth
        uint64_t m_FrameCount = 0;  // detects first frame (history invalid)
        uint32_t m_LastW = 0, m_LastH = 0;
    };

} // namespace Ayaya
