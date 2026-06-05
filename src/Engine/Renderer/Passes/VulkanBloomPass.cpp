#include "ayapch.h"
#include "VulkanBloomPass.hpp"
#include "Renderer/RenderGraph.hpp"

namespace Ayaya {

    VulkanBloomPass::VulkanBloomPass() {
        m_PassName = "Bloom Pass";
    }

    void VulkanBloomPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        builder.ReadTexture("Lighting");
        FramebufferSpecification spec;
        spec.Width  = width;
        spec.Height = height;
        spec.Samples = 1;
        spec.Attachments = { FramebufferTextureFormat::RGBA16F };
        builder.WriteTexture("Bloom", spec);
    }

    void VulkanBloomPass::OnAttach() {
        m_DownsampleShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/bloom_downsample.frag");
        m_UpsampleShader   = Shader::Create("PostProcess/postprocess.vert", "PostProcess/bloom_upsample.frag");
    }

    void VulkanBloomPass::OnResize(uint32_t width, uint32_t height) {
        // Internal mips 1-4 (mip 0 is the RenderGraph-managed "Bloom" FBO)
        m_InternalMips.clear();
        glm::vec2 mipSize((float)width / 4.0f, (float)height / 4.0f); // start at 1/4 (already halved once from input)

        for (uint32_t i = 0; i < 4; i++) {
            VulkanBloomMip mip;
            mip.Size = mipSize;
            mip.IntSize = glm::vec2((float)(uint32_t)mipSize.x, (float)(uint32_t)mipSize.y);

            FramebufferSpecification spec;
            spec.Width  = (uint32_t)mip.IntSize.x;
            spec.Height = (uint32_t)mip.IntSize.y;
            spec.Samples = 1;
            spec.Attachments = { FramebufferTextureFormat::RGBA16F };
            mip.FBO = Framebuffer::Create(spec);

            m_InternalMips.push_back(mip);
            mipSize *= 0.5f;
        }
    }

    void VulkanBloomPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto inputFBO = context.GetFramebuffer("Lighting");
        auto bloomFBO = context.GetFramebuffer("Bloom");
        if (!inputFBO || !bloomFBO) return;

        bool enableBloom = context.Get<bool>("EnableBloom", false);
        float threshold = context.Get<float>("BloomThreshold", 1.0f);
        float knee      = context.Get<float>("BloomKnee", 0.1f);
        float radius    = context.Get<float>("BloomRadius", 0.005f);

        if (!enableBloom) {
            // Clear bloom output to black so PostProcess blend is a no-op
            cmd.BeginRenderPass(bloomFBO, true, glm::vec4(0.0f));
            cmd.EndRenderPass();
            context.Framebuffers["Bloom"] = bloomFBO;
            return;
        }

        // Resize internal mips on first frame or viewport change
        uint32_t vpW = context.Get<uint32_t>("ViewportWidth");
        uint32_t vpH = context.Get<uint32_t>("ViewportHeight");
        if (m_InternalMips.empty() || m_LastVPWidth != vpW || m_LastVPHeight != vpH) {
            OnResize(vpW, vpH);
            m_LastVPWidth  = vpW;
            m_LastVPHeight = vpH;
        }

        // Lazy pipeline creation (FBO-dependent, so defer to first Execute)
        if (!m_DownsamplePipeline) {
            PipelineSpecification ds;
            ds.Shader = m_DownsampleShader;
            ds.TargetFramebuffer = bloomFBO;
            ds.Layout = {};
            ds.DepthTest = false; ds.DepthWrite = false; ds.Blend = false;
            ds.BackfaceCulling = CullMode::None;
            m_DownsamplePipeline = Pipeline::Create(ds);

            PipelineSpecification us;
            us.Shader = m_UpsampleShader;
            us.TargetFramebuffer = bloomFBO;
            us.Layout = {};
            us.DepthTest = false; us.DepthWrite = false;
            us.Blend = true; us.BlendMode = BlendModeType::Additive;
            us.BackfaceCulling = CullMode::None;
            m_UpsamplePipeline = Pipeline::Create(us);
        }

        glm::vec3 curve(threshold - knee, knee * 2.0f, 0.25f / knee);

        // ---- Downsample (Lighting → mip[0]=Bloom → mip[1] → mip[2] → mip[3]) ----
        cmd.BindPipeline(m_DownsamplePipeline);

        struct {
            std::shared_ptr<Framebuffer> FBO;
            glm::vec2 Size;
        } chain[5];
        chain[0] = { bloomFBO, glm::vec2((float)vpW / 2.0f, (float)vpH / 2.0f) };
        for (int i = 0; i < 4; i++) chain[i+1] = { m_InternalMips[i].FBO, m_InternalMips[i].Size };

        for (int i = 0; i < 5; i++) {
            auto src = (i == 0) ? inputFBO : chain[i-1].FBO;
            cmd.BeginRenderPass(chain[i].FBO, false, glm::vec4(0.0f));
            cmd.BindTexture2D(m_DownsamplePipeline, "u_Image", 0, src, 0);

            BloomDownsamplePushConstants pc{};
            pc.TexelSize = 1.0f / chain[i].Size;
            pc.MipLevel  = i;
            pc.Threshold = threshold;
            pc.Curve     = curve;
            cmd.PushConstantData(m_DownsamplePipeline, &pc, sizeof pc);
            cmd.DrawArrays(3);
            cmd.EndRenderPass();
        }

        // ---- Upsample (mip[3] → ... → mip[0]=Bloom) ----
        cmd.BindPipeline(m_UpsamplePipeline);

        BloomUpsamplePushConstants upPC{};
        upPC.FilterRadius = radius;
        cmd.PushConstantData(m_UpsamplePipeline, &upPC, sizeof upPC);

        for (int i = 3; i >= 0; i--) {
            auto src = chain[i+1].FBO;
            cmd.BeginRenderPass(chain[i].FBO, false, glm::vec4(0.0f));
            cmd.BindTexture2D(m_UpsamplePipeline, "u_Image", 0, src, 0);
            cmd.DrawArrays(3);
            cmd.EndRenderPass();
        }

        context.Framebuffers["Bloom"] = bloomFBO;
    }

}
