#include "ayapch.h"
#include "VulkanPostProcessPass.hpp"

namespace Ayaya {

    VulkanPostProcessPass::VulkanPostProcessPass() {
        m_PassName = "Vulkan Post Process Pass";
    }

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
        builder.ReadTexture("SceneColor");
        builder.ReadTexture("Selection");

        FramebufferSpecification finalSpec;
        finalSpec.Width  = width;
        finalSpec.Height = height;
        finalSpec.Samples = 1;
        finalSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
        builder.WriteTexture("FinalOutput", finalSpec);
    }

    void VulkanPostProcessPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width  = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");
        if (width == 0 || height == 0) return;

        auto finalFBO = context.GetFramebuffer("FinalOutput");
        if (!finalFBO) {
            AYAYA_CORE_ERROR("VulkanPostProcessPass: FinalOutput FBO not found!");
            return;
        }

        if (!m_Pipeline) {
            m_PipeSpec.TargetFramebuffer = finalFBO;
            m_Pipeline = Pipeline::Create(m_PipeSpec);
        }

        cmd.BeginRenderPass(finalFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;

        // 获取上游场景颜色 — 优先从 Graph 黑板取，兼容旧的 context key
        std::shared_ptr<Framebuffer> sceneFBO = context.GetFramebuffer("SceneColor");
        if (!sceneFBO) sceneFBO = context.GetFramebuffer("ForwardTest");
        if (!sceneFBO) sceneFBO = context.GetFramebuffer("Lighting");

        auto whiteTex = context.GetTexture("WhiteTexture");
        auto blackTex = context.GetTexture("BlackTexture");

        if (sceneFBO) cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, sceneFBO, 0);
        else          cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, whiteTex);

        auto selectionFBO = context.GetFramebuffer("Selection");
        if (selectionFBO) cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, selectionFBO, 0);
        else              cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, blackTex);

        auto bloomFBO = context.GetFramebuffer("Bloom");
        bool bloomEnabled = (bloomFBO != nullptr) && context.Get<bool>("EnableBloom", true);
        if (bloomEnabled) cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, bloomFBO, 0);
        else              cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, blackTex);

        PostProcessPushConstants pc{};
        float physExposure = context.Get<float>("PhysicalExposure", 1.0f);
        float expComp = context.Get<float>("ExposureCompensation", 1.0f);
        pc.Exposure = physExposure * expComp;
        pc.ToneMappingType = context.Get<int>("ToneMappingType", 1);
        pc.TexelSize = glm::vec2(1.0f / (float)width, 1.0f / (float)height);
        pc.EnableBloom = bloomEnabled ? 1 : 0;
        pc.BloomIntensity = context.Get<float>("BloomIntensity", 1.0f);

        cmd.PushConstantData(m_Pipeline, &pc, sizeof(PostProcessPushConstants));

        if (context.RecordAndCheckDrawCall("Post Process Pass", "Tone Mapping & Combine", "PostProcess Shader", 1))
            cmd.DrawArrays(m_EmptyVAO, 3);

        cmd.EndRenderPass();
        // RenderGraph 自动插入 Barrier
    }

}
