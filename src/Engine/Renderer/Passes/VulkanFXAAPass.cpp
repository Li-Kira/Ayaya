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

        FramebufferSpecification spec;
        spec.Samples = 1;
        spec.Width = 1280;
        spec.Height = 720;
        spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_FXAAFBO = Framebuffer::Create(spec);

        PipelineSpecification pipelineSpec;
        pipelineSpec.Shader = m_FXAAShader;
        pipelineSpec.TargetFramebuffer = m_FXAAFBO;
        pipelineSpec.Layout = {};
        pipelineSpec.DepthTest = false;
        pipelineSpec.DepthWrite = false;
        pipelineSpec.Blend = false;
        pipelineSpec.BackfaceCulling = CullMode::None;

        m_Pipeline = Pipeline::Create(pipelineSpec);
    }

    void VulkanFXAAPass::OnResize(uint32_t width, uint32_t height) {
        m_FXAAFBO->Resize(width, height);
    }

    void VulkanFXAAPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        std::shared_ptr<Framebuffer> inputFBO;

        if (context.Framebuffers.find("PostProcess") != context.Framebuffers.end()) {
            inputFBO = context.Framebuffers["PostProcess"];
        } else {
            inputFBO = context.Get<std::shared_ptr<Framebuffer>>("PostProcess_Output", nullptr);
        }

        bool isFXAAActive = context.Get<bool>("EnableFXAA", true);

        if (!inputFBO || !isFXAAActive) {
            if (inputFBO) {
                context.Set("Final_Output", std::dynamic_pointer_cast<void>(inputFBO));
            }
            return;
        }

        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");
        if (width == 0 || height == 0) return;

        if (m_FXAAFBO->GetSpecification().Width != width || m_FXAAFBO->GetSpecification().Height != height) {
            OnResize(width, height);
        }

        cmd.BeginRenderPass(m_FXAAFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;

        cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, inputFBO, 0);

        FXAAPushConstants constants{};
        constants.TexelSize = glm::vec2(
            1.0f / (float)m_FXAAFBO->GetSpecification().Width,
            1.0f / (float)m_FXAAFBO->GetSpecification().Height
        );
        cmd.PushConstantData(m_Pipeline, &constants, sizeof(FXAAPushConstants));

        if (context.RecordAndCheckDrawCall("FXAA Pass", "Anti-Aliasing", "FXAA Shader", 1)) {
            cmd.DrawArrays(3);
        }

        cmd.EndRenderPass();
        // 过渡到只读布局供 ImGui 采样
        cmd.TransitionImageLayout(m_FXAAFBO, 0, ImageLayout::ColorAttachmentOptimal, ImageLayout::ShaderReadOnlyOptimal);
        cmd.InsertExecutionBarrier();

        context.Set("Final_Output", std::dynamic_pointer_cast<void>(m_FXAAFBO));
        context.Framebuffers["FXAA"] = m_FXAAFBO;
    }

}
