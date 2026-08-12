#include "ayapch.h"
#include "VulkanSSRBlurPass.hpp"
#include "Renderer/RenderGraph.hpp"

namespace Ayaya {

    VulkanSSRBlurPass::VulkanSSRBlurPass() { m_PassName = "SSR Blur"; }

    void VulkanSSRBlurPass::DeclareResources(RGBuilder& builder,
                                              uint32_t width, uint32_t height) {
        builder.ReadTexture("SSR_Result");
        builder.ReadTexture("GBuffer");
        builder.ReadTexture("SceneDepth");
        FramebufferSpecification s;
        s.Width = width / 2; s.Height = height / 2; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F };
        builder.WriteTexture("SSR_Blurred", s);
    }

    void VulkanSSRBlurPass::OnAttach() {
        m_BlurShader = Shader::Create("SSR/ssr_blur.vert", "SSR/ssr_blur.frag");
        if (!m_BlurShader) {
            AYAYA_CORE_ERROR("[SSRBlur] Failed to create blur shader!");
            return;
        }

        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = { FramebufferTextureFormat::RGBA16F };
        m_RefFBO = Framebuffer::Create(ref);

        PipelineSpecification ps;
        ps.Shader = m_BlurShader; ps.TargetFramebuffer = m_RefFBO;
        ps.Layout = {}; ps.Topology = PrimitiveTopology::TriangleStrip;
        ps.DepthTest = false; ps.DepthWrite = false; ps.Blend = false;
        ps.BackfaceCulling = CullMode::None;
        m_BlurXPipeline = Pipeline::Create(ps);
        m_BlurYPipeline = Pipeline::Create(ps);
        if (!m_BlurXPipeline || !m_BlurYPipeline)
            AYAYA_CORE_ERROR("[SSRBlur] Failed to create blur pipelines!");
    }

    void VulkanSSRBlurPass::Execute(RenderContext& context,
                                     RenderCommandBuffer& cmd) {
        if (!context.Get<bool>("EnableSSR", false)) return;

        auto ssrFBO      = context.GetFramebuffer("SSR_Result");
        auto blurredFBO  = context.GetFramebuffer("SSR_Blurred");
        auto gbufferFBO  = context.GetFramebuffer("GBuffer");
        auto depthFBO    = context.GetFramebuffer("SceneDepth");
        if (!ssrFBO || !blurredFBO || !gbufferFBO || !depthFBO) return;

        uint32_t vpW = context.Get<uint32_t>("ViewportWidth");
        uint32_t vpH = context.Get<uint32_t>("ViewportHeight");
        uint32_t hw = vpW / 2, hh = vpH / 2;
        if (!vpW || !vpH) return;

        // ── Resize internal FBO if needed ──
        if (hw != m_LastW || hh != m_LastH) {
            m_LastW = hw; m_LastH = hh;
            FramebufferSpecification s;
            s.Width = hw; s.Height = hh; s.Samples = 1;
            s.Attachments = { FramebufferTextureFormat::RGBA16F };
            m_BlurXFBO = Framebuffer::Create(s);
        }
        if (!m_BlurXFBO) return;

        struct alignas(16) BlurPC {
            glm::mat4 InverseViewProj;   // 64
            glm::vec2 BlurDir;           //  8
            glm::vec2 TexelSize;         //  8
            float     DepthThreshold;    //  4
            float     _pad;              //  4
        } pc;
        pc.InverseViewProj = glm::inverse(context.ProjectionMatrix * context.ViewMatrix);
        pc.DepthThreshold  = 30.0f;

        // ── Sub-pass 1: Blur X → internal FBO ──
        {
            cmd.TransitionImageLayout(m_BlurXFBO, 0,
                ImageLayout::ShaderReadOnlyOptimal, ImageLayout::ColorAttachmentOptimal);
            cmd.BeginRenderPass(m_BlurXFBO, true, glm::vec4(0.0f));
            cmd.BindPipeline(m_BlurXPipeline);
            cmd.BindTexture2D(m_BlurXPipeline, "u_SSRResult",   0, ssrFBO, 0);
            cmd.BindTexture2D(m_BlurXPipeline, "g_PBR",         1, gbufferFBO, 2);
            cmd.BindTexture2D(m_BlurXPipeline, "u_DepthMap",    2, depthFBO, 0, true);
            pc.BlurDir   = glm::vec2(1.0f, 0.0f);
            pc.TexelSize = glm::vec2(1.0f / float(hw), 1.0f / float(hh));
            cmd.PushConstantData(m_BlurXPipeline, &pc, sizeof pc);
            cmd.DrawArrays(3);
            cmd.EndRenderPass();
            cmd.TransitionImageLayout(m_BlurXFBO, 0,
                ImageLayout::ColorAttachmentOptimal, ImageLayout::ShaderReadOnlyOptimal);
        }

        // ── Sub-pass 2: Blur Y → RenderGraph-managed SSR_Blurred ──
        {
            cmd.BeginRenderPass(blurredFBO, true, glm::vec4(0.0f));
            cmd.BindPipeline(m_BlurYPipeline);
            cmd.BindTexture2D(m_BlurYPipeline, "u_SSRResult",   0, m_BlurXFBO, 0);
            cmd.BindTexture2D(m_BlurYPipeline, "g_PBR",         1, gbufferFBO, 2);
            cmd.BindTexture2D(m_BlurYPipeline, "u_DepthMap",    2, depthFBO, 0, true);
            pc.BlurDir   = glm::vec2(0.0f, 1.0f);
            pc.TexelSize = glm::vec2(1.0f / float(hw), 1.0f / float(hh));
            cmd.PushConstantData(m_BlurYPipeline, &pc, sizeof pc);
            cmd.DrawArrays(3);
            cmd.EndRenderPass();
        }

        context.Framebuffers["SSR_Blurred"] = blurredFBO;
    }

} // namespace Ayaya
