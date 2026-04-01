#include "ayapch.h"
#include "FXAAPass.hpp"

namespace Ayaya {

    FXAAPass::FXAAPass() {
        m_PassName = "FXAA Pass";
    }

    void FXAAPass::OnAttach() {
        // 【核心改变】：使用引擎抽象创建空 VAO，不再调用 glGenVertexArrays
        m_EmptyVAO.reset(VertexArray::Create());

        m_FXAAShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/fxaa.frag");

        FramebufferSpecification spec;
        spec.Samples = 1;
        spec.Width = 1280; 
        spec.Height = 720;
        spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_FXAAFBO = Framebuffer::Create(spec);
    }

    void FXAAPass::OnResize(uint32_t width, uint32_t height) {
        m_FXAAFBO->Resize(width, height);
    }

    // 【修改】：接收 cmd 对象
    void FXAAPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t inputTextureID = context.Get<uint32_t>("PostProcess_Output", 0);
        bool enableFXAA = context.Get<bool>("EnableFXAA", true);

        if (inputTextureID == 0 || !enableFXAA) {
            context.Set("Final_Output", inputTextureID);
            return;
        }

        m_FXAAFBO->Bind();
        
        // ==========================================
        // 全部改为由 Command Buffer 接管管线状态！
        // ==========================================
        cmd.SetViewport(0, 0, m_FXAAFBO->GetSpecification().Width, m_FXAAFBO->GetSpecification().Height);
        cmd.SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        cmd.Clear();
        cmd.SetDepthTest(false);

        m_FXAAShader->Bind();
        context.Stats.ShaderBinds++;
        m_FXAAShader->SetInt("u_ScreenTexture", 0);
        
        glm::vec2 texelSize = {
            1.0f / (float)m_FXAAFBO->GetSpecification().Width,
            1.0f / (float)m_FXAAFBO->GetSpecification().Height
        };
        m_FXAAShader->SetFloat2("u_TexelSize", texelSize); 

        // 交给 cmd 绑定纹理
        cmd.BindTexture2D(0, inputTextureID);

        // 【拦截验证】：抗锯齿处理
        if (context.RecordAndCheckDrawCall("FXAA Pass", "Anti-Aliasing", "FXAA Shader", 1)) {
            // 交给 cmd 执行绑定 VAO 与绘制！
            cmd.DrawArrays(m_EmptyVAO, 3);
        }
        
        m_FXAAFBO->Unbind();

        context.Set("Final_Output", m_FXAAFBO->GetColorAttachmentRendererID(0));
    }
}