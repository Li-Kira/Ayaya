#include "ayapch.h"
#include "VulkanPostProcessPass.hpp"

namespace Ayaya {

    VulkanPostProcessPass::VulkanPostProcessPass() { m_PassName = "Post Process"; }

    void VulkanPostProcessPass::OnAttach() {
        m_EmptyVAO = VertexArray::Create();
        m_PostProcessShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/postprocess.frag");
        m_PipeSpec.Shader = m_PostProcessShader;
        m_PipeSpec.Layout = {};
        m_PipeSpec.Topology = PrimitiveTopology::TriangleStrip;
        m_PipeSpec.DepthTest = false;
        m_PipeSpec.DepthWrite = false;
        m_PipeSpec.Blend = false;
        m_PipeSpec.BackfaceCulling = CullMode::None;
    }

    void VulkanPostProcessPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        builder.ReadTexture("TAA_Output");
        builder.ReadTexture("Selection");
        builder.ReadTexture("Bloom");
        FramebufferSpecification s;
        s.Width = width; s.Height = height; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA8 };
        builder.WriteTexture("FinalOutput", s);
    }

    void VulkanPostProcessPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t w = context.Get<uint32_t>("ViewportWidth"), h = context.Get<uint32_t>("ViewportHeight");
        if (!w || !h) return;
        auto fbo = context.GetFramebuffer("FinalOutput");
        if (!fbo) return;
        if (!m_Pipeline) { m_PipeSpec.TargetFramebuffer = fbo; m_Pipeline = Pipeline::Create(m_PipeSpec); }

        cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
        cmd.BindPipeline(m_Pipeline);
        auto src = context.GetFramebuffer("TAA_Output");
        if (!src) src = context.GetFramebuffer("Lighting");  // SRP path (no TAA pass)
        auto wt = context.GetTexture("WhiteTexture");
        if (src) cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, src, 0);
        else cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, wt);
        auto sel = context.GetFramebuffer("Selection");
        auto bloom = context.GetFramebuffer("Bloom");
        auto bt  = context.GetTexture("BlackTexture");
        if (sel) cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, sel, 0);
        else if (bt) cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, bt);
        if (bloom) cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, bloom, 0);
        else if (bt) cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, bt);
        PostProcessPushConstants pc{};
        float physExposure = context.Get<float>("PhysicalExposure", 1.0f);
        float expComp = context.Get<float>("ExposureCompensation", 1.0f);
        pc.Exposure = physExposure * expComp;
        pc.ToneMappingType = context.Get<int>("ToneMappingType", 1);
        pc.TexelSize = glm::vec2(1.f/w, 1.f/h);
        pc.EnableBloom = context.Get<bool>("EnableBloom", false) ? 1 : 0;
        pc.BloomIntensity = context.Get<float>("BloomIntensity", 1.0f);
        glm::vec3 tint = context.Get<glm::vec3>("BloomTint", glm::vec3(1.0f));
        pc.BloomTintR = tint.r;
        pc.BloomTintG = tint.g;
        pc.BloomTintB = tint.b;
        cmd.PushConstantData(m_Pipeline, &pc, sizeof pc);
        cmd.DrawArrays(m_EmptyVAO, 3);
        cmd.EndRenderPass();
        context.Framebuffers["FinalOutput"] = fbo;
    }
}
