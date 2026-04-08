#include "ayapch.h"
#include "FXAAPass.hpp"
#include <glm/glm.hpp>

namespace Ayaya {

    FXAAPass::FXAAPass() {
        m_PassName = "FXAA Pass";
    }

    void FXAAPass::OnAttach() {
        // 创建独立的全屏绘制 VAO
        m_EmptyVAO = VertexArray::Create();

        // 加载 Shader
        m_FXAAShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/fxaa.frag");

        // 创建专属的 FBO
        FramebufferSpecification spec;
        spec.Samples = 1;
        spec.Width = 1280; 
        spec.Height = 720;
        spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_FXAAFBO = Framebuffer::Create(spec);

        // ==========================================
        // 【核心新增】：将渲染状态提前固化为管线图纸 (PSO)
        // ==========================================
        PipelineSpecification pipelineSpec;
        pipelineSpec.Shader = m_FXAAShader;
        pipelineSpec.TargetFramebuffer = m_FXAAFBO;
        pipelineSpec.DepthTest = false;  // FXAA 是全屏覆盖，不需要深度测试
        pipelineSpec.DepthWrite = false; // 不需要写入深度
        pipelineSpec.Blend = false;      // 直接覆盖像素即可

        m_Pipeline = Pipeline::Create(pipelineSpec);
    }

    void FXAAPass::OnResize(uint32_t width, uint32_t height) {
        m_FXAAFBO->Resize(width, height);
    }

    void FXAAPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        // 1. 从黑板获取上一阶段 (PostProcess) 的输出纹理ID
        void* rawInputTex = context.Get<void*>("PostProcess_Output", nullptr);
        uint32_t inputTextureID = (uint32_t)(intptr_t)rawInputTex;
        
        // 2. 检查 FXAA 是否在 UI 中被开启
        bool isFXAAActive = context.Get<bool>("EnableFXAA", true);

        // 如果没有输入图，或者 FXAA 被关闭，直接把输入图作为最终结果传出去！
        if (inputTextureID == 0 || !isFXAAActive) {
            context.Set("Final_Output", rawInputTex);
            return;
        }

        // 3. 开始执行 FXAA 渲染
        cmd.BeginRenderPass(m_FXAAFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        
        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;
        
        cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, inputTextureID);
        
        glm::vec2 texelSz = glm::vec2(
            1.0f / (float)m_FXAAFBO->GetSpecification().Width,
            1.0f / (float)m_FXAAFBO->GetSpecification().Height
        );
        cmd.PushConstant(m_Pipeline, "u_TexelSize", texelSz);

        // 执行绘制
        if (context.RecordAndCheckDrawCall("FXAA Pass", "Anti-Aliasing", "FXAA Shader", 1)) {
            cmd.DrawArrays(m_EmptyVAO, 3);
        }
        
        cmd.EndRenderPass();

        // 4. 将抗锯齿后的结果，写回黑板，供 EditorLayer 视口读取！
        context.Set("Final_Output", m_FXAAFBO->GetColorAttachmentRendererID(0));
    }

}