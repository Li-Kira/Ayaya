#include "ayapch.h"
#include "VulkanPostProcessPass.hpp"
#include "Core/Application.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"

namespace Ayaya {

    VulkanPostProcessPass::VulkanPostProcessPass() {
        m_PassName = "Vulkan Post Process Pass";
    }

    void VulkanPostProcessPass::OnAttach() {
        m_EmptyVAO = VertexArray::Create();

        m_PostProcessShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/postprocess.frag");

        FramebufferSpecification postSpec;
        postSpec.Samples = 1; 
        postSpec.Width = 1280; 
        postSpec.Height = 720;
        postSpec.Attachments = { FramebufferTextureFormat::RGBA8 }; // 后处理不需要深度缓冲

        m_PostProcessFBOs.resize(3);
        for (int i = 0; i < 3; i++) {
            m_PostProcessFBOs[i] = Framebuffer::Create(postSpec);
        }

        PipelineSpecification pipelineSpec;
        pipelineSpec.Shader = m_PostProcessShader;
        pipelineSpec.TargetFramebuffer = m_PostProcessFBOs[0];
        pipelineSpec.Layout = {}; // 魔法大三角形，不需要顶点输入
        pipelineSpec.DepthTest = false;  
        pipelineSpec.DepthWrite = false; 
        pipelineSpec.Blend = false;      
        pipelineSpec.BackfaceCulling = CullMode::None; 

        m_Pipeline = Pipeline::Create(pipelineSpec);
    }

    void VulkanPostProcessPass::OnResize(uint32_t width, uint32_t height) {
        for (auto& fbo : m_PostProcessFBOs) {
            fbo->Resize(width, height);
        }
    }

    void VulkanPostProcessPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");
        if (width == 0 || height == 0) return;

        auto vulkanContext = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        uint32_t frameIndex = vulkanContext->GetCurrentFrameIndex() % m_PostProcessFBOs.size();
        auto currentFBO = m_PostProcessFBOs[frameIndex];

        if (currentFBO->GetSpecification().Width != width || currentFBO->GetSpecification().Height != height) {
            OnResize(width, height);
        }

        cmd.BeginRenderPass(currentFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;

        // ==========================================
        // 1. 获取主画面 (抓取之前 VulkanForwardTestPass 的产物)
        // ==========================================
        std::shared_ptr<Framebuffer> screenFBO;
        if (context.Framebuffers.find("ForwardTest") != context.Framebuffers.end()) {
            screenFBO = context.Framebuffers["ForwardTest"];
        }

        auto whiteTex = context.GetTexture("WhiteTexture");

        // Vulkan 规定所有描述符槽位都必须绑定有效数据，否则会报错
        if (screenFBO) {
            cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, screenFBO, 0);
        } else {
            cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, whiteTex);
        }

        // ==========================================
        // 2. 绑定杂项 (描边、泛光)
        // ==========================================
        std::shared_ptr<Framebuffer> selectionFBO;
        if (context.Framebuffers.find("Selection") != context.Framebuffers.end()) selectionFBO = context.Framebuffers["Selection"];
        if (selectionFBO) cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, selectionFBO, 0);
        else cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, whiteTex);

        std::shared_ptr<Framebuffer> bloomFBO;
        if (context.Framebuffers.find("Bloom") != context.Framebuffers.end()) bloomFBO = context.Framebuffers["Bloom"];
        bool isBloomEnabled = (bloomFBO != nullptr) && context.Get<bool>("EnableBloom", true);
        
        if (isBloomEnabled) cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, bloomFBO, 0);
        else cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, whiteTex);

        // ==========================================
        // 3. 推送参数
        // ==========================================
        PostProcessPushConstants pc{};
        float physExposure = context.Get<float>("PhysicalExposure", 1.0f);
        float expComp = context.Get<float>("ExposureCompensation", 1.0f);
        
        pc.Exposure = physExposure * expComp;
        pc.ToneMappingType = context.Get<int>("ToneMappingType", 1);
        pc.TexelSize = glm::vec2(1.0f / (float)width, 1.0f / (float)height);
        pc.EnableBloom = isBloomEnabled ? 1 : 0;
        pc.BloomIntensity = context.Get<float>("BloomIntensity", 1.0f);

        cmd.PushConstantData(m_Pipeline, &pc, sizeof(PostProcessPushConstants));

        // ==========================================
        // 4. 绘制大三角形
        // ==========================================
        if (context.RecordAndCheckDrawCall("Post Process Pass", "Tone Mapping & Combine", "PostProcess Shader", 1)) {
            // 没有顶点缓冲，直接调用 3 个顶点即可
            cmd.DrawArrays(m_EmptyVAO, 3);
        }
        
        cmd.EndRenderPass();

        // 最终输出挂载到黑板上
        context.Set("Final_Output", currentFBO);
        context.Set("PostProcess_Output", currentFBO);
        context.Framebuffers["PostProcess"] = currentFBO;
    }

}