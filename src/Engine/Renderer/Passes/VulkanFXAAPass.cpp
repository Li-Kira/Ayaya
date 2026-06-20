#include "ayapch.h"
#include "VulkanFXAAPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include <glm/glm.hpp>

namespace Ayaya {

    VulkanFXAAPass::VulkanFXAAPass() {
        m_PassName = "FXAA Pass";
    }

    void VulkanFXAAPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        builder.ReadTexture("FinalOutput");
        FramebufferSpecification spec;
        spec.Width  = width;
        spec.Height = height;
        spec.Samples = 1;
        spec.Attachments = { FramebufferTextureFormat::RGBA8 };
        builder.WriteTexture("FXAA", spec);
    }

    void VulkanFXAAPass::OnAttach() {
        m_FXAAShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/fxaa.frag");

        // Minimal reference FBO for pipeline format deduction only.
        FramebufferSpecification refSpec;
        refSpec.Samples = 1;
        refSpec.Width = 1280;
        refSpec.Height = 720;
        refSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
        m_RefFBO = Framebuffer::Create(refSpec);

        PipelineSpecification pipeSpec;
        pipeSpec.Shader = m_FXAAShader;
        pipeSpec.TargetFramebuffer = m_RefFBO;
        pipeSpec.Layout = {};
        pipeSpec.Topology = PrimitiveTopology::TriangleStrip;
        pipeSpec.DepthTest = false;
        pipeSpec.DepthWrite = false;
        pipeSpec.Blend = false;
        pipeSpec.BackfaceCulling = CullMode::None;
        m_Pipeline = Pipeline::Create(pipeSpec);
    }

    void VulkanFXAAPass::OnResize(uint32_t width, uint32_t height) {
        // RenderGraph-managed "FXAA" FBO handles resizing automatically.
    }

    void VulkanFXAAPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto inputFBO  = context.GetFramebuffer("FinalOutput");
        auto outputFBO = context.GetFramebuffer("FXAA");
        if (!inputFBO || !outputFBO) return;

        uint32_t w = outputFBO->GetSpecification().Width;
        uint32_t h = outputFBO->GetSpecification().Height;
        if (!w || !h) return;

        cmd.BeginRenderPass(outputFBO, true, glm::vec4(0.0f));
        cmd.BindPipeline(m_Pipeline);
        cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, inputFBO, 0);

        FXAAPushConstants pc{};
        pc.TexelSize = glm::vec2(1.0f / (float)w, 1.0f / (float)h);
        cmd.PushConstantData(m_Pipeline, &pc, sizeof pc);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();

        context.Framebuffers["FXAA"] = outputFBO;
    }

}
