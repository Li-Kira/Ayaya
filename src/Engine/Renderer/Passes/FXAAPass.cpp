#include "ayapch.h"
#include "FXAAPass.hpp"
#include <glm/glm.hpp>

namespace Ayaya {

    FXAAPass::FXAAPass() {
        m_PassName = "FXAA Pass";
    }

    void FXAAPass::OnAttach() {
        // 创建独立的全屏绘制 VAO
        m_EmptyVAO.reset(VertexArray::Create());

        // 加载 Shader
        m_FXAAShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/fxaa.frag");

        // 创建专属的 FBO
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

    void FXAAPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        // 1. 从黑板获取上一阶段 (PostProcess) 的输出纹理ID
        uint32_t inputTextureID = context.Get<uint32_t>("PostProcess_Output", 0);
        
        // 2. 检查 FXAA 是否在 UI 中被开启
        bool isFXAAActive = context.Get<bool>("EnableFXAA", true);

        // 如果没有输入图，或者 FXAA 被关闭，直接把输入图作为最终结果传出去！
        if (inputTextureID == 0 || !isFXAAActive) {
            context.Set("Final_Output", inputTextureID);
            return;
        }

        // 3. 开始执行 FXAA 渲染
        m_FXAAFBO->Bind();
        cmd.SetViewport(0, 0, m_FXAAFBO->GetSpecification().Width, m_FXAAFBO->GetSpecification().Height);
        
        // 使用圆括号初始化，避开 MSVC 常量报错
        cmd.SetClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        cmd.Clear();
        cmd.SetDepthTest(false);

        m_FXAAShader->Bind();
        context.Stats.ShaderBinds++;
        m_FXAAShader->SetInt("u_ScreenTexture", 0);
        
        glm::vec2 texelSz = glm::vec2(
            1.0f / (float)m_FXAAFBO->GetSpecification().Width,
            1.0f / (float)m_FXAAFBO->GetSpecification().Height
        );
        m_FXAAShader->SetFloat2("u_TexelSize", texelSz); 

        // 绑定输入图
        cmd.BindTexture2D(0, inputTextureID);

        // 执行绘制
        if (context.RecordAndCheckDrawCall("FXAA Pass", "Anti-Aliasing", "FXAA Shader", 1)) {
            cmd.DrawArrays(m_EmptyVAO, 3);
        }
        
        m_FXAAFBO->Unbind();

        // 4. 将抗锯齿后的结果，写回黑板，供 EditorLayer 视口读取！
        context.Set("Final_Output", m_FXAAFBO->GetColorAttachmentRendererID(0));
    }

}