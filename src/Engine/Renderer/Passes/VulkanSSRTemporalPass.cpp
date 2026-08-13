#include "ayapch.h"
#include "VulkanSSRTemporalPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    VulkanSSRTemporalPass::VulkanSSRTemporalPass() { m_PassName = "SSR Temporal"; }

    void VulkanSSRTemporalPass::DeclareResources(RGBuilder& builder,
                                                   uint32_t width, uint32_t height) {
        builder.ReadTexture("SSR_Blurred");
        builder.ReadTexture("SceneDepth");
        builder.ReadTexture("GBuffer");

        FramebufferSpecification s;
        s.Width = width / 2; s.Height = height / 2; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F };
        builder.WriteTexture("SSR_Temporal", s);
    }

    void VulkanSSRTemporalPass::OnAttach() {
        m_Shader = Shader::Create("SSR/ssr_temporal.vert", "SSR/ssr_temporal.frag");
        if (!m_Shader) {
            AYAYA_CORE_ERROR("[SSRTemporal] Failed to create shader!");
            return;
        }

        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = { FramebufferTextureFormat::RGBA16F };
        m_RefFBO = Framebuffer::Create(ref);

        PipelineSpecification ps;
        ps.Shader = m_Shader; ps.TargetFramebuffer = m_RefFBO;
        ps.Layout = {}; ps.Topology = PrimitiveTopology::TriangleStrip;
        ps.DepthTest = false; ps.DepthWrite = false; ps.Blend = false;
        ps.BackfaceCulling = CullMode::None;
        m_Pipeline = Pipeline::Create(ps);
        if (!m_Pipeline)
            AYAYA_CORE_ERROR("[SSRTemporal] Failed to create pipeline!");
    }

    void VulkanSSRTemporalPass::Execute(RenderContext& context,
                                         RenderCommandBuffer& cmd) {
        if (!context.Get<bool>("EnableSSR", false)) return;

        auto ssrBlurredFBO = context.GetFramebuffer("SSR_Blurred");
        auto temporalFBO   = context.GetFramebuffer("SSR_Temporal");
        auto gbufferFBO    = context.GetFramebuffer("GBuffer");
        auto depthFBO      = context.GetFramebuffer("SceneDepth");
        if (!ssrBlurredFBO || !temporalFBO || !gbufferFBO || !depthFBO) return;

        uint32_t vpW = context.Get<uint32_t>("ViewportWidth");
        uint32_t vpH = context.Get<uint32_t>("ViewportHeight");
        uint32_t hw = vpW / 2, hh = vpH / 2;
        if (!vpW || !vpH || !hw || !hh) return;

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        uint32_t fi = vkCtx->GetCurrentFrameIndex() % 3;

        // ── Resize history buffers ──
        if (hw != m_LastW || hh != m_LastH) {
            m_LastW = hw; m_LastH = hh;
            FramebufferSpecification s;
            s.Width = hw; s.Height = hh; s.Samples = 1;
            s.Attachments = { FramebufferTextureFormat::RGBA16F };
            for (int i = 0; i < 3; i++)
                m_HistoryFBO[i] = Framebuffer::Create(s);
            m_FrameCount = 0;
        }

        uint32_t currIdx = fi;
        uint32_t prevIdx = (fi + 2) % 3;  // previous frame's slot
        bool hasHistory = m_FrameCount > 0 && m_HistoryFBO[prevIdx] != nullptr;

        // ── Render temporal blend → RenderGraph-managed SSR_Temporal ──
        cmd.BeginRenderPass(temporalFBO, true, glm::vec4(0.0f));
        cmd.BindPipeline(m_Pipeline);

        // Set 1 bindings
        cmd.BindTexture2D(m_Pipeline, "u_SSRBlurred",   0, ssrBlurredFBO, 0);
        cmd.BindTexture2D(m_Pipeline, "u_Velocity",      1, gbufferFBO, 4);
        cmd.BindTexture2D(m_Pipeline, "u_DepthMap",      2, depthFBO, 0, true);
        cmd.BindTexture2D(m_Pipeline, "g_Normal",        3, gbufferFBO, 0);
        cmd.BindTexture2D(m_Pipeline, "g_PBR",           4, gbufferFBO, 2);
        // History: previous frame's temporally accumulated result.
        // On first frame fall back to current SSR_Blurred (no history yet).
        if (hasHistory)
            cmd.BindTexture2D(m_Pipeline, "u_SSRHistory", 5, m_HistoryFBO[prevIdx], 0);
        else
            cmd.BindTexture2D(m_Pipeline, "u_SSRHistory", 5, ssrBlurredFBO, 0);

        struct alignas(16) TemporalPC {
            float DepthThreshold;
            float NormalThreshold;
            float TemporalBlend;
            float _pad;
            float TexelSizeW;
            float TexelSizeH;
            float _pad2a;
            float _pad2b;
        } pc;
        pc.DepthThreshold  = 0.05f;
        pc.NormalThreshold = 0.8f;
        pc.TemporalBlend   = context.Get<float>("SSR_TemporalBlend", 0.05f);  // min blend: current-frame weight
        pc.TexelSizeW = 1.0f / float(hw);
        pc.TexelSizeH = 1.0f / float(hh);
        cmd.PushConstantData(m_Pipeline, &pc, sizeof pc);

        context.RecordAndCheckDrawCall("SSR Temporal", "SSR_Temporal", "ssr_temporal", 1);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();

        // ── Save to history ──
        // The RenderGraph triple-buffers SSR_Temporal, so storing its FBO reference
        // gives us cross-frame persistence for free. Frame N reads history from
        // slot (N-1)%3, which still holds frame N-1's data (written 1 frame ago).
        m_HistoryFBO[currIdx] = temporalFBO;
        m_FrameCount++;

        context.Framebuffers["SSR_Temporal"] = temporalFBO;
    }

} // namespace Ayaya
