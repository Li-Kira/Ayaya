#include "ayapch.h"
#include "VulkanPostProcessPass.hpp"
#include <glm/glm.hpp>

namespace Ayaya {

    VulkanPostProcessPass::VulkanPostProcessPass() {
        m_PassName = "Vulkan Post Process Pass";
    }

    void VulkanPostProcessPass::OnAttach() {
        // 1. 加载基于逻辑路径的新架构 Shader
        m_PostProcessShader = Shader::Create("postprocess/postprocess.vert", "postprocess/postprocess.frag");

        // 2. 创建专属 FBO
        FramebufferSpecification postSpec;
        postSpec.Samples = 1; 
        postSpec.Width = 1280; 
        postSpec.Height = 720;
        postSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_PostProcessFBO = Framebuffer::Create(postSpec);

        // 3. 固化管线 (PSO)
        PipelineSpecification pipelineSpec;
        pipelineSpec.Shader = m_PostProcessShader;
        pipelineSpec.TargetFramebuffer = m_PostProcessFBO;
        pipelineSpec.DepthTest = false;  
        pipelineSpec.DepthWrite = false; 
        pipelineSpec.Blend = false;      
        // 【核心】：全屏三角形必须关闭背面剔除
        pipelineSpec.BackfaceCulling = CullMode::None; 

        m_Pipeline = Pipeline::Create(pipelineSpec);
    }

    void VulkanPostProcessPass::OnResize(uint32_t width, uint32_t height) {
        m_PostProcessFBO->Resize(width, height);
    }

    void VulkanPostProcessPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");

        if (width == 0 || height == 0) return;

        if (m_PostProcessFBO->GetSpecification().Width != width || m_PostProcessFBO->GetSpecification().Height != height) {
            OnResize(width, height);
        }

        // ==========================================
        // 1. 还原并保留所有 OpenGL 版本的输入源获取逻辑
        // ==========================================
        std::shared_ptr<Framebuffer> screenFBO;
        uint32_t screenAttachmentIndex = 0;

        // 优先拾取 Lighting 阶段的高清光照图，如果没有则退化使用 VulkanTarget 纯色底图
        if (context.Framebuffers.find("Lighting") != context.Framebuffers.end()) {
            screenFBO = context.Framebuffers["Lighting"];
            screenAttachmentIndex = 0; 
        } else if (context.Framebuffers.find("VulkanTarget") != context.Framebuffers.end()) {
            screenFBO = context.Framebuffers["VulkanTarget"];
        }

        std::shared_ptr<Framebuffer> selectionFBO;
        if (context.Framebuffers.find("Selection") != context.Framebuffers.end()) {
            selectionFBO = context.Framebuffers["Selection"];
        }

        std::shared_ptr<Framebuffer> bloomFBO;
        if (context.Framebuffers.find("Bloom") != context.Framebuffers.end()) {
            bloomFBO = context.Framebuffers["Bloom"];
        }

        bool isBloomEnabled = (bloomFBO != nullptr) && context.Get<bool>("EnableBloom", true);

        // ==========================================
        // 2. 绑定贴图并处理降级防崩 (Vulkan 必须填满所有的 Descriptor 坑位)
        // ==========================================
        if (screenFBO) {
            cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, screenFBO, screenAttachmentIndex);
        }
        
        auto whiteTex = context.GetTexture("WhiteTexture");
        if (whiteTex) {
            // 保证 Binding 1 和 2 绝对不为空
            cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, whiteTex);
            cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, whiteTex);
        }

        // if (selectionFBO) {
        //     cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, selectionFBO, 0);
        // } else if (screenFBO) {
        //     cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, screenFBO, screenAttachmentIndex);
        // }

        // if (isBloomEnabled) {
        //     cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, bloomFBO, 0);
        // } else if (screenFBO) {
        //     cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, screenFBO, screenAttachmentIndex);
        // }

        // ==========================================
        // 3. 执行内存屏障！等待前置 Pass (Lighting/Bloom/Selection) 全部写入完成
        // ==========================================
        cmd.InsertExecutionBarrier();

        // ==========================================
        // 4. 开启后处理渲染通道
        // ==========================================
        cmd.BeginRenderPass(m_PostProcessFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        
        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;

        // ==========================================
        // 5. 组装并推送常量块 (彻底抛弃 m_EmptyVAO)
        // ==========================================
        PostProcessPushConstants constants{};
        
        float physExposure = context.Get<float>("PhysicalExposure", 1.0f);
        float expComp = context.Get<float>("ExposureCompensation", 1.0f);
        
        constants.Exposure = physExposure * expComp;
        constants.TexelSize = glm::vec2(1.0f / (float)width, 1.0f / (float)height);
        constants.ToneMappingType = context.Get<int>("ToneMappingType", 1);
        constants.EnableBloom = isBloomEnabled ? 1 : 0;
        constants.BloomIntensity = context.Get<float>("BloomIntensity", 1.0f);

        cmd.PushConstantData(m_Pipeline, &constants, sizeof(PostProcessPushConstants));

        if (context.RecordAndCheckDrawCall("Post Process Pass", "Tone Mapping & Combine", "PostProcess Shader", 1)) {
            cmd.DrawArrays(3); // 原生触发全屏绘制
        }
        
        cmd.EndRenderPass();

        // ==========================================
        // 6. 产出交还黑板
        // ==========================================
        context.Set("PostProcess_Output", m_PostProcessFBO);
        context.Set("Final_Output", m_PostProcessFBO); // 如果后面没有 FXAA，这就是最终输出
        context.Framebuffers["PostProcess"] = m_PostProcessFBO;
    }
}