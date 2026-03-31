#include "ayapch.h"
#include "BloomPass.hpp"
#include "Renderer/RenderCommand.hpp"
#include <glad/glad.h>

namespace Ayaya {

    BloomPass::BloomPass() {
        m_PassName = "Bloom Pass";
    }

    BloomPass::~BloomPass() {
        if (m_EmptyVAO != 0) glDeleteVertexArrays(1, &m_EmptyVAO);
    }

    void BloomPass::OnAttach() {
        glGenVertexArrays(1, &m_EmptyVAO);

        m_BloomExtractShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/bloom_extract.frag");
        m_BloomBlurShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/bloom_blur.frag");

        FramebufferSpecification bloomSpec;
        bloomSpec.Samples = 1; 
        bloomSpec.Width = 1280 / 2; bloomSpec.Height = 720 / 2;
        bloomSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth }; 
        m_BloomFBO[0] = Framebuffer::Create(bloomSpec);
        m_BloomFBO[1] = Framebuffer::Create(bloomSpec);
    }

    void BloomPass::OnResize(uint32_t width, uint32_t height) {
        m_BloomFBO[0]->Resize(width / 2, height / 2);
        m_BloomFBO[1]->Resize(width / 2, height / 2);
    }

    void BloomPass::Execute(RenderContext& context) {
        bool enableBloom = context.Get<bool>("EnableBloom", true);
        if (!enableBloom) {
            context.Set("Bloom_Output", (uint32_t)0); // 没开泛光，给下游传个 0
            return;
        }

        uint32_t inputTextureID = context.Get<uint32_t>("Lighting_Output", 0);
        if (inputTextureID == 0) return;

        float threshold = context.Get<float>("BloomThreshold", 1.0f);
        float physicalExposure = context.Get<float>("PhysicalExposure", 1.0f);
        float exposureComp = context.Get<float>("ExposureCompensation", 1.0f);

        // --- 1. 高光提取 ---
        m_BloomFBO[0]->Bind();
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        RenderCommand::Clear();
        glDisable(GL_DEPTH_TEST);
        
        m_BloomExtractShader->Bind();
        context.Stats.ShaderBinds++;
        m_BloomExtractShader->SetInt("u_ScreenTexture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTextureID);
        m_BloomExtractShader->SetFloat("u_Threshold", threshold);
        m_BloomExtractShader->SetFloat("u_Exposure", physicalExposure * exposureComp);
        
        glBindVertexArray(m_EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        context.Stats.DrawCalls++; context.Stats.TriangleCount += 1; context.Stats.VertexCount += 3;
        
        // --- 2. 乒乓高斯模糊 ---
        bool horizontal = true, first_iteration = true;
        int amount = 10; 
        m_BloomBlurShader->Bind();
        context.Stats.ShaderBinds++;
        m_BloomBlurShader->SetInt("u_Image", 0);
        
        for (int i = 0; i < amount; i++) {
            m_BloomFBO[horizontal]->Bind();
            m_BloomBlurShader->SetBool("u_Horizontal", horizontal);
            
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, first_iteration ? m_BloomFBO[0]->GetColorAttachmentRendererID(0) : m_BloomFBO[!horizontal]->GetColorAttachmentRendererID(0));
            
            glDrawArrays(GL_TRIANGLES, 0, 3);
            context.Stats.DrawCalls++; context.Stats.TriangleCount += 1; context.Stats.VertexCount += 3;
            horizontal = !horizontal;
            if (first_iteration) first_iteration = false;
        }
        m_BloomFBO[0]->Unbind();

        // 3. 将结果挂载到黑板上！
        context.Set("Bloom_Output", m_BloomFBO[0]->GetColorAttachmentRendererID(0));
    }
}