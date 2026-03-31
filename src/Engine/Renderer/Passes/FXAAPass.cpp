#include "ayapch.h"
#include "FXAAPass.hpp"
#include "Renderer/RenderCommand.hpp"
#include <glad/glad.h>

namespace Ayaya {

    FXAAPass::FXAAPass() {
        m_PassName = "FXAA Pass";
    }

    FXAAPass::~FXAAPass() {
        if (m_EmptyVAO != 0) {
            glDeleteVertexArrays(1, &m_EmptyVAO);
        }
    }

    void FXAAPass::OnAttach() {
        // 创建独立的全屏绘制 VAO
        glGenVertexArrays(1, &m_EmptyVAO);

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

    void FXAAPass::Execute(RenderContext& context) {
        // 1. 从黑板获取上一阶段 (PostProcess) 的输出纹理ID
        uint32_t inputTextureID = context.Get<uint32_t>("PostProcess_Output", 0);
        
        // 2. 检查 FXAA 是否在 UI 中被开启
        bool enableFXAA = context.Get<bool>("EnableFXAA", true);

        // 如果没有输入图，或者 FXAA 被关闭，直接把输入图作为最终结果传出去！
        if (inputTextureID == 0 || !enableFXAA) {
            context.Set("Final_Output", inputTextureID);
            return;
        }

        // 3. 开始执行 FXAA 渲染
        m_FXAAFBO->Bind();
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        RenderCommand::Clear();
        glDisable(GL_DEPTH_TEST);

        m_FXAAShader->Bind();
        context.Stats.ShaderBinds++;
        m_FXAAShader->SetInt("u_ScreenTexture", 0);
        
        glm::vec2 texelSize = {
            1.0f / (float)m_FXAAFBO->GetSpecification().Width,
            1.0f / (float)m_FXAAFBO->GetSpecification().Height
        };
        m_FXAAShader->SetFloat2("u_TexelSize", texelSize); 

        // 绑定输入图
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTextureID);

        glBindVertexArray(m_EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        context.Stats.DrawCalls++; context.Stats.TriangleCount += 1; context.Stats.VertexCount += 3;
        
        m_FXAAFBO->Unbind();

        // 4. 将抗锯齿后的结果，写回黑板，供 EditorLayer 视口读取！
        context.Set("Final_Output", m_FXAAFBO->GetColorAttachmentRendererID(0));

        // 【新增】：将 FBO 实体挂载到黑板，供高清截图读取像素！
        context.Framebuffers["FXAA"] = m_FXAAFBO;
        // TODO: 未来可以通过 Context 将 Stats 回传给渲染器统计
    }

}