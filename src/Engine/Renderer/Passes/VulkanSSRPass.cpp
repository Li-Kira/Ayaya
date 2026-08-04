#include "ayapch.h"
#include "VulkanSSRPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    VulkanSSRPass::VulkanSSRPass() { m_PassName = "SSR"; }

    void VulkanSSRPass::DeclareResources(RGBuilder& builder,
                                          uint32_t width, uint32_t height) {
        builder.ReadTexture("SceneDepth");
        builder.ReadTexture("GBuffer");
        builder.ReadTexture("Lighting");
        FramebufferSpecification s;
        s.Width = width / 2;  s.Height = height / 2;  s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F };
        builder.WriteTexture("SSR_Result", s);
    }

    void VulkanSSRPass::OnAttach() {
        m_MarchShader = Shader::Create("SSR/ssr_march.vert", "SSR/ssr_march.frag");
        if (!m_MarchShader) {
            AYAYA_CORE_ERROR("[SSRPass] Failed to create SSR march shader!");
            return;
        }

        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = { FramebufferTextureFormat::RGBA16F };
        m_RefFBO = Framebuffer::Create(ref);

        PipelineSpecification ps;
        ps.Shader = m_MarchShader; ps.TargetFramebuffer = m_RefFBO; ps.Layout = {};
        ps.Topology = PrimitiveTopology::TriangleStrip;
        ps.DepthTest = false; ps.DepthWrite = false; ps.Blend = false;
        ps.BackfaceCulling = CullMode::None;
        m_MarchPipeline = Pipeline::Create(ps);
        if (!m_MarchPipeline)
            AYAYA_CORE_ERROR("[SSRPass] Failed to create SSR march pipeline!");
    }

    void VulkanSSRPass::OnResize(uint32_t width, uint32_t height) {
        uint32_t hw = width / 2, hh = height / 2;
        if (hw == m_LastW && hh == m_LastH) return;
        m_LastW = hw; m_LastH = hh;
        FramebufferSpecification s;
        s.Width = hw; s.Height = hh; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F };
        m_ReflectionFBO = Framebuffer::Create(s);
    }

    void VulkanSSRPass::Execute(RenderContext& context,
                                 RenderCommandBuffer& cmd) {
        if (!context.Get<bool>("EnableSSR", false)) return;

        auto gbufferFBO    = context.GetFramebuffer("GBuffer");
        auto sceneDepthFBO = context.GetFramebuffer("SceneDepth");
        auto lightingFBO   = context.GetFramebuffer("Lighting");
        auto ssrFBO        = context.GetFramebuffer("SSR_Result");
        if (!gbufferFBO || !sceneDepthFBO || !lightingFBO || !ssrFBO) return;

        uint32_t vpW = context.Get<uint32_t>("ViewportWidth");
        uint32_t vpH = context.Get<uint32_t>("ViewportHeight");
        if (!vpW || !vpH) return;

        if (vpW / 2 != m_LastW || vpH / 2 != m_LastH)
            OnResize(vpW, vpH);
        if (!m_ReflectionFBO) return;

        // ── Ray March (half-res → RenderGraph-managed SSR_Result) ──
        // NOTE: No manual TransitionImageLayout — the RenderGraph handles
        // EnsureWritable (→COLOR_ATTACHMENT) pre-pass and
        // InsertTileResolveBarrier (→SHADER_READ_ONLY) post-pass automatically.
        cmd.BeginRenderPass(ssrFBO, true, glm::vec4(0.0f));
        cmd.BindPipeline(m_MarchPipeline);
        cmd.BindTexture2D(m_MarchPipeline, "u_DepthMap", 0, sceneDepthFBO, 0, true);
        cmd.BindTexture2D(m_MarchPipeline, "g_Normal",   4, gbufferFBO, 0);
        cmd.BindTexture2D(m_MarchPipeline, "g_Albedo",   1, gbufferFBO, 1);
        cmd.BindTexture2D(m_MarchPipeline, "g_PBR",      2, gbufferFBO, 2);
        cmd.BindTexture2D(m_MarchPipeline, "u_Lighting", 6, lightingFBO, 0);

        struct alignas(16) SSR_PC {
            glm::mat4 InvProj;         // 64  @ 0
            glm::mat4 Proj;            // 64  @ 64
            glm::mat4 View;            // 64  @ 128
            float MaxSteps;            // 4   @ 192
            float StepSize;            // 4   @ 196
            float Thickness;           // 4   @ 200
            float EdgeFade;            // 4   @ 204
            int   MaxBinarySteps;      // 4   @ 208
            float RoughnessCutoff;     // 4   @ 212
            int   Enabled;             // 4   @ 216
            float _pad;                // 4   @ 220
        } pc;
        pc.InvProj  = glm::inverse(context.ProjectionMatrix);
        pc.Proj     = context.ProjectionMatrix;
        pc.View     = context.ViewMatrix;
        pc.MaxSteps = context.Get<float>("SSR_MaxSteps", 64.0f);
        pc.StepSize = context.Get<float>("SSR_StepSize", 0.5f);
        pc.Thickness = context.Get<float>("SSR_Thickness", 0.3f);
        pc.EdgeFade = context.Get<float>("SSR_EdgeFade", 0.1f);
        pc.MaxBinarySteps = context.Get<int>("SSR_MaxBinarySteps", 8);
        pc.RoughnessCutoff = context.Get<float>("SSR_RoughnessCutoff", 1.0f);
        pc.Enabled = 1;
        pc._pad = 0.0f;
        cmd.PushConstantData(m_MarchPipeline, &pc, sizeof pc);
        context.RecordAndCheckDrawCall("SSR", "SSR_Result", "ssr_march", 1);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();

        context.Framebuffers["SSR_Result"] = ssrFBO;
    }

} // namespace Ayaya
