#include "ayapch.h"
#include "VulkanSSAOPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    std::shared_ptr<Texture2D> VulkanSSAOPass::s_NoiseTexture;

    VulkanSSAOPass::VulkanSSAOPass() { m_PassName = "SSAO"; }

    void VulkanSSAOPass::DeclareResources(RGBuilder& builder,
                                           uint32_t width, uint32_t height) {
        builder.ReadTexture("GBuffer");
        builder.ReadTexture("SceneDepth");
        // Only expose the final AO texture; intermediate targets are
        // managed internally to avoid RenderGraph layout tracking conflicts.
        FramebufferSpecification s;
        s.Width = width / 2;  s.Height = height / 2;  s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::R8 };
        builder.WriteTexture("SSAO_Final", s);
    }

    std::shared_ptr<Texture2D> VulkanSSAOPass::GetNoiseTexture() {
        if (!s_NoiseTexture) {
            // Generate 16 random 2D unit-circle directions packed in [0,1] for UNORM storage.
            // Stored as RGBA8_UNORM → negative values would be clamped, so we pack into [0,1].
            glm::vec4 noise[16];
            for (int i = 0; i < 16; i++) {
                float angle = (float)rand() / (float)RAND_MAX * 2.0f * glm::pi<float>();
                float x = cos(angle) * 0.5f + 0.5f;  // [-1,1] → [0,1]
                float y = sin(angle) * 0.5f + 0.5f;  // [-1,1] → [0,1]
                noise[i] = glm::vec4(x, y, 0.0f, 0.0f);
            }
            s_NoiseTexture = Texture2D::Create(4, 4);
            s_NoiseTexture->SetData(noise, sizeof(noise));
        }
        return s_NoiseTexture;
    }

    void VulkanSSAOPass::ReleaseNoiseTexture() {
        s_NoiseTexture.reset();
    }

    void VulkanSSAOPass::OnAttach() {
        m_GenShader  = Shader::Create("PostProcess/postprocess.vert", "SSAO/ssao_generate.frag");
        m_BlurShader = Shader::Create("PostProcess/postprocess.vert", "SSAO/ssao_blur.frag");

        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = { FramebufferTextureFormat::R8 };
        m_RefFBO = Framebuffer::Create(ref);

        PipelineSpecification gs;
        gs.Shader = m_GenShader; gs.TargetFramebuffer = m_RefFBO; gs.Layout = {};
        gs.Topology = PrimitiveTopology::TriangleStrip;
        gs.DepthTest = false; gs.DepthWrite = false; gs.Blend = false;
        gs.BackfaceCulling = CullMode::None;
        m_GenPipeline = Pipeline::Create(gs);

        PipelineSpecification bs;
        bs.Shader = m_BlurShader; bs.TargetFramebuffer = m_RefFBO; bs.Layout = {};
        bs.Topology = PrimitiveTopology::TriangleStrip;
        bs.DepthTest = false; bs.DepthWrite = false; bs.Blend = false;
        bs.BackfaceCulling = CullMode::None;
        m_BlurXPipeline = Pipeline::Create(bs);

        PipelineSpecification bsY = bs;
        m_BlurYPipeline = Pipeline::Create(bsY);
    }

    void VulkanSSAOPass::OnResize(uint32_t width, uint32_t height) {
        uint32_t hw = width / 2, hh = height / 2;
        if (hw == m_LastW && hh == m_LastH) return;
        m_LastW = hw; m_LastH = hh;
        FramebufferSpecification s;
        s.Width = hw; s.Height = hh; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::R8 };
        m_RawFBO  = Framebuffer::Create(s);
        m_BlurXFBO = Framebuffer::Create(s);
    }

    void VulkanSSAOPass::Execute(RenderContext& context,
                                  RenderCommandBuffer& cmd) {
        // Runtime check — allows dynamic toggle without graph rebuild
        if (!context.Get<bool>("EnableSSAO", false)) return;

        auto gbufferFBO = context.GetFramebuffer("GBuffer");
        auto sceneDepthFBO = context.GetFramebuffer("SceneDepth");
        auto finalFBO   = context.GetFramebuffer("SSAO_Final");
        if (!gbufferFBO || !sceneDepthFBO || !finalFBO) return;

        uint32_t vpW = context.Get<uint32_t>("ViewportWidth");
        uint32_t vpH = context.Get<uint32_t>("ViewportHeight");
        if (!vpW || !vpH) return;

        if (vpW / 2 != m_LastW || vpH / 2 != m_LastH)
            OnResize(vpW, vpH);
        if (!m_RawFBO || !m_BlurXFBO) return;

        auto noiseTex = GetNoiseTexture();
        if (!noiseTex) return;

        // ---- 1. Generate SSAO (half-res, internal FBO) ----
        cmd.TransitionImageLayout(m_RawFBO, 0,
            ImageLayout::ShaderReadOnlyOptimal, ImageLayout::ColorAttachmentOptimal);
        cmd.BeginRenderPass(m_RawFBO, true, glm::vec4(1.0f));
        cmd.BindPipeline(m_GenPipeline);
        // World-space SSAO: reconstruct position from linear depth
        cmd.BindTexture2D(m_GenPipeline, "u_DepthMap", 0, sceneDepthFBO, 0, true);
        cmd.BindTexture2D(m_GenPipeline, "g_Normal",   1, gbufferFBO, 0);
        cmd.BindTexture2D(m_GenPipeline, "u_Noise",    2, noiseTex);

        struct GenPC {
            alignas(16) glm::mat4 InverseViewProj;
            glm::vec2 NoiseScale; float Radius; float Bias; float Power; int SampleCount; int _pad;
        } gen;
        gen.InverseViewProj = context.Get<glm::mat4>("InverseViewProj", glm::mat4(1.0f));
        gen.NoiseScale = glm::vec2((float)vpW / 4.0f, (float)vpH / 4.0f);
        gen.Radius = context.Get<float>("SSAORadius", 0.15f);
        gen.Bias   = context.Get<float>("SSAOBias", 0.05f);
        gen.Power  = 2.0f;
        gen.SampleCount = 32;
        gen._pad = 0;
        cmd.PushConstantData(m_GenPipeline, &gen, sizeof gen);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();
        cmd.TransitionImageLayout(m_RawFBO, 0,
            ImageLayout::ColorAttachmentOptimal, ImageLayout::ShaderReadOnlyOptimal);

        // ---- 2. Blur X (internal FBO) ----
        cmd.TransitionImageLayout(m_BlurXFBO, 0,
            ImageLayout::ShaderReadOnlyOptimal, ImageLayout::ColorAttachmentOptimal);
        cmd.BeginRenderPass(m_BlurXFBO, true, glm::vec4(1.0f));
        cmd.BindPipeline(m_BlurXPipeline);
        cmd.BindTexture2D(m_BlurXPipeline, "u_AO",       0, m_RawFBO, 0);
        cmd.BindTexture2D(m_BlurXPipeline, "u_DepthMap", 1, sceneDepthFBO, 0, true);
        cmd.BindTexture2D(m_BlurXPipeline, "g_Normal",   2, gbufferFBO, 0);
        struct BlurPC {
            alignas(16) glm::mat4 InverseViewProj;
            alignas(8) glm::vec2 BlurDir; alignas(8) glm::vec2 TexelSize;
            float DepthThresh; float _pad;
        } bx;
        bx.InverseViewProj = context.Get<glm::mat4>("InverseViewProj", glm::mat4(1.0f));
        bx.BlurDir = {1,0};
        bx.TexelSize = { 1.0f/(float)(vpW/2), 1.0f/(float)(vpH/2) };
        bx.DepthThresh = 20.0f;  // exponential falloff sharpness
        bx._pad = 0;
        cmd.PushConstantData(m_BlurXPipeline, &bx, sizeof bx);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();
        cmd.TransitionImageLayout(m_BlurXFBO, 0,
            ImageLayout::ColorAttachmentOptimal, ImageLayout::ShaderReadOnlyOptimal);

        // ---- 3. Blur Y → RenderGraph-managed "SSAO_Final" ----
        cmd.BeginRenderPass(finalFBO, true, glm::vec4(1.0f));
        cmd.BindPipeline(m_BlurYPipeline);
        cmd.BindTexture2D(m_BlurYPipeline, "u_AO",       0, m_BlurXFBO, 0);
        cmd.BindTexture2D(m_BlurYPipeline, "u_DepthMap", 1, sceneDepthFBO, 0, true);
        cmd.BindTexture2D(m_BlurYPipeline, "g_Normal",   2, gbufferFBO, 0);
        BlurPC by;
        by.InverseViewProj = context.Get<glm::mat4>("InverseViewProj", glm::mat4(1.0f));
        by.BlurDir = {0,1};
        by.TexelSize = bx.TexelSize;
        by.DepthThresh = 20.0f;  // exponential falloff sharpness
        by._pad = 0;
        cmd.PushConstantData(m_BlurYPipeline, &by, sizeof by);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();

        context.Framebuffers["SSAO_Final"] = finalFBO;
    }

}
