#include "ayapch.h"
#include "VulkanTAAPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    VulkanTAAPass::VulkanTAAPass() { m_PassName = "TAA Pass"; }

    void VulkanTAAPass::DeclareResources(RGBuilder& builder,
                                          uint32_t width, uint32_t height) {
        builder.ReadTexture("Lighting");
        builder.ReadTexture("SceneDepth");
        builder.ReadTexture("GBuffer");

        FramebufferSpecification s;
        s.Width = width; s.Height = height; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F };
        builder.WriteTexture("TAA_Output", s);
    }

    void VulkanTAAPass::OnAttach() {
        m_Shader = Shader::Create("PostProcess/postprocess.vert", "TAA/taa.frag");
        if (!m_Shader) {
            AYAYA_CORE_ERROR("[TAA] Failed to create shader!");
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
            AYAYA_CORE_ERROR("[TAA] Failed to create pipeline!");
    }

    void VulkanTAAPass::Execute(RenderContext& context,
                                 RenderCommandBuffer& cmd) {
        auto lightingFBO = context.GetFramebuffer("Lighting");
        auto outputFBO   = context.GetFramebuffer("TAA_Output");
        auto gbufferFBO  = context.GetFramebuffer("GBuffer");
        auto depthFBO    = context.GetFramebuffer("SceneDepth");
        if (!lightingFBO || !outputFBO || !gbufferFBO || !depthFBO) return;

        uint32_t w = outputFBO->GetSpecification().Width;
        uint32_t h = outputFBO->GetSpecification().Height;
        if (!w || !h) return;

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        uint32_t fi = vkCtx->GetCurrentFrameIndex() % 3;

        // ── Resize history buffers ──
        if (w != m_LastW || h != m_LastH) {
            m_LastW = w; m_LastH = h;
            FramebufferSpecification s;
            s.Width = w; s.Height = h; s.Samples = 1;
            s.Attachments = { FramebufferTextureFormat::RGBA16F };
            for (int i = 0; i < 3; i++) {
                m_HistoryFBO[i] = Framebuffer::Create(s);
                m_DepthHistoryFBO[i] = nullptr;  // stale size — invalidate
            }
            m_FrameCount = 0;
        }

        uint32_t currIdx = fi;
        uint32_t prevIdx = (fi + 2) % 3;
        bool hasHistory = m_FrameCount > 0 && m_HistoryFBO[prevIdx] != nullptr;

        cmd.BeginRenderPass(outputFBO, true, glm::vec4(0.0f));
        cmd.BindPipeline(m_Pipeline);

        // Set 1 bindings
        cmd.BindTexture2D(m_Pipeline, "u_SceneColor",   0, lightingFBO, 0);
        cmd.BindTexture2D(m_Pipeline, "u_Velocity",     1, gbufferFBO, 4);
        cmd.BindTexture2D(m_Pipeline, "u_DepthMap",     2, depthFBO, 0, true);
        cmd.BindTexture2D(m_Pipeline, "g_Normal",       3, gbufferFBO, 0);
        // History (previous frame TAA_Output + SceneDepth); first frame falls back
        // to current-frame inputs so the blend degenerates gracefully.
        if (hasHistory) {
            cmd.BindTexture2D(m_Pipeline, "u_DepthHistory", 4, m_DepthHistoryFBO[prevIdx], 0, true);
            cmd.BindTexture2D(m_Pipeline, "u_History",      5, m_HistoryFBO[prevIdx], 0);
        } else {
            cmd.BindTexture2D(m_Pipeline, "u_DepthHistory", 4, depthFBO, 0, true);
            cmd.BindTexture2D(m_Pipeline, "u_History",      5, lightingFBO, 0);
        }

        TAAPushConstants pc{};
        pc.InvViewProjection  = context.Get<glm::mat4>("InverseViewProj", glm::mat4(1.0f));
        pc.PrevViewProjection = context.Get<glm::mat4>("PrevViewProjectionMatrix", pc.InvViewProjection);
        pc.Jitter             = context.Get<glm::vec2>("TAA_Jitter", glm::vec2(0.0f));
        pc.TexelSize          = glm::vec2(1.0f / (float)w, 1.0f / (float)h);
        pc.BlendFactor        = context.Get<float>("TAA_BlendFactor", 0.05f);
        pc.DepthThreshold     = context.Get<float>("TAA_DepthThreshold", 0.05f);
        pc.NormalThreshold    = context.Get<float>("TAA_NormalThreshold", 0.8f);
        pc.MotionThreshold    = context.Get<float>("TAA_MotionThreshold", 0.1f);
        pc.SharpenAmount      = context.Get<float>("TAA_SharpenAmount", 0.15f);
        pc.Enable             = context.Get<bool>("EnableTAA", false) ? 1.0f : 0.0f;
        cmd.PushConstantData(m_Pipeline, &pc, sizeof pc);

        context.RecordAndCheckDrawCall("TAA", "TAA_Output", "taa", 1);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();

        // ── Save to history (RenderGraph triple-buffered FBO pointers) ──
        m_HistoryFBO[currIdx]      = outputFBO;
        m_DepthHistoryFBO[currIdx] = depthFBO;
        m_FrameCount++;

        context.Framebuffers["TAA_Output"] = outputFBO;
    }

} // namespace Ayaya
