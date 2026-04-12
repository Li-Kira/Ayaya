#include "ayapch.h"
#include "VulkanPostProcessPass.hpp"
#include <glm/glm.hpp>

namespace Ayaya {

    VulkanPostProcessPass::VulkanPostProcessPass() {
        m_PassName = "Post Process Pass";
    }

    void VulkanPostProcessPass::OnAttach() {
        m_EmptyVAO = VertexArray::Create();

        m_PostProcessShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/postprocess.frag");

        FramebufferSpecification postSpec;
        postSpec.Samples = 1; 
        postSpec.Width = 1280; postSpec.Height = 720;
        postSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_PostProcessFBO = Framebuffer::Create(postSpec);

        // ==========================================
        // 【核心新增】：把渲染状态前置打包为管线图纸 (PSO)
        // ==========================================
        PipelineSpecification pipelineSpec;
        pipelineSpec.Shader = m_PostProcessShader;
        pipelineSpec.TargetFramebuffer = m_PostProcessFBO;
        pipelineSpec.DepthTest = false;  
        pipelineSpec.DepthWrite = false; 
        pipelineSpec.Blend = false;      
        
        // 【核心修复】：彻底关闭背面剔除！确保无论顺逆时针，三角形绝对会被画出来！
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

        // 1. 同步离线画布尺寸
        if (m_PostProcessFBO->GetSpecification().Width != width || m_PostProcessFBO->GetSpecification().Height != height) {
            m_PostProcessFBO->Resize(width, height);
        }

        // 2. 拿到 VulkanClearPass 画好的底图，并提前喂给 Descriptor Set
        std::shared_ptr<Framebuffer> inputFBO;
        if (context.Framebuffers.find("VulkanTarget") != context.Framebuffers.end()) {
            inputFBO = context.Framebuffers["VulkanTarget"];
        }

        if (inputFBO) {
            cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, inputFBO, 0);
            // 填饱 1 和 2 号槽位，防止 Vulkan 验证层报野指针错误
            cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, inputFBO, 0);
            cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, inputFBO, 0);
        }

        // ==========================================
        // 【核心同步】：设置管线屏障！
        // 告诉 GPU：必须等前一个 Pass (ClearPass) 把颜色彻底写进 FBO，
        // 并且布局转换完成后，才能往下执行我的 Fragment Shader！
        // ==========================================
        cmd.InsertExecutionBarrier();

        // 3. 开启渲染录制
        cmd.BeginRenderPass(m_PostProcessFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        
        // 绑定装好贴图的管线
        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;

        // 4. 召唤全屏大三角形！
        if (context.RecordAndCheckDrawCall("Post Process Pass", "Texture Verification", "PostProcess Shader", 1)) {
            cmd.DrawArrays(3); 
        }

        cmd.EndRenderPass();

        // 5. 将处理好的画面交给 ImGui 视口
        context.Framebuffers["PostProcess"] = m_PostProcessFBO;
        context.Set("PostProcess_Output", std::dynamic_pointer_cast<void>(m_PostProcessFBO));
        context.Set("Final_Output", std::dynamic_pointer_cast<void>(m_PostProcessFBO));
    }
}