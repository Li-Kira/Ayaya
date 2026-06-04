#include "ayapch.h"
#include "VulkanPostProcessPass.hpp"

namespace Ayaya {

    VulkanPostProcessPass::VulkanPostProcessPass() { m_PassName = "Post Process"; }

    void VulkanPostProcessPass::OnAttach() {
        m_EmptyVAO = VertexArray::Create();
        m_PostProcessShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/postprocess.frag");
        m_PipeSpec.Shader = m_PostProcessShader;
        m_PipeSpec.Layout = {};
        m_PipeSpec.DepthTest = false;
        m_PipeSpec.DepthWrite = false;
        m_PipeSpec.Blend = false;
        m_PipeSpec.BackfaceCulling = CullMode::None;
    }

    void VulkanPostProcessPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        builder.ReadTexture("Lighting");
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
        auto src = context.GetFramebuffer("Lighting");
        auto wt = context.GetTexture("WhiteTexture");
        if (src) cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, src, 0);
        else cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, wt);
        auto bt = context.GetTexture("BlackTexture");
        if (bt) { cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, bt); cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, bt); }
        PostProcessPushConstants pc{};
        float physExposure = context.Get<float>("PhysicalExposure", 1.0f);
        float expComp = context.Get<float>("ExposureCompensation", 1.0f);
        pc.Exposure = physExposure * expComp;
        pc.ToneMappingType = context.Get<int>("ToneMappingType", 1);
        pc.TexelSize = glm::vec2(1.f/w, 1.f/h);
        cmd.PushConstantData(m_Pipeline, &pc, sizeof pc);
        cmd.DrawArrays(m_EmptyVAO, 3);
        cmd.EndRenderPass();
        context.Framebuffers["FinalOutput"] = fbo;
    }
}
