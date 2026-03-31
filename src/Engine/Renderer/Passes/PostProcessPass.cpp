#include "ayapch.h"
#include "PostProcessPass.hpp"
#include "Renderer/RenderCommand.hpp"
#include <glad/glad.h>

namespace Ayaya {

    PostProcessPass::PostProcessPass() {
        m_PassName = "Post Process Pass";
    }

    PostProcessPass::~PostProcessPass() {
        if (m_EmptyVAO != 0) glDeleteVertexArrays(1, &m_EmptyVAO);
    }

    void PostProcessPass::OnAttach() {
        glGenVertexArrays(1, &m_EmptyVAO);

        m_PostProcessShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/postprocess.frag");

        FramebufferSpecification postSpec;
        postSpec.Samples = 1; 
        postSpec.Width = 1280; postSpec.Height = 720;
        postSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_PostProcessFBO = Framebuffer::Create(postSpec);
    }

    void PostProcessPass::OnResize(uint32_t width, uint32_t height) {
        m_PostProcessFBO->Resize(width, height);
    }

    void PostProcessPass::Execute(RenderContext& context) {
        // 视锥体剔除、排序、画网格，然后传递 4 张 G-Buffer 贴图和深度图
        uint32_t lightingTexID = context.Get<uint32_t>("Lighting_Output", 0);
        uint32_t selectionTexID = context.Get<uint32_t>("Selection_Output", 0);
        uint32_t bloomTexID = context.Get<uint32_t>("Bloom_Output", 0);
        
        if (lightingTexID == 0) return;

        m_PostProcessFBO->Bind();
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        RenderCommand::Clear();
        glDisable(GL_DEPTH_TEST); 

        m_PostProcessShader->Bind();
        context.Stats.ShaderBinds++;

        // 基础贴图
        m_PostProcessShader->SetInt("u_ScreenTexture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, lightingTexID);

        m_PostProcessShader->SetInt("u_SelectionTexture", 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, selectionTexID);

        // 参数注入
        float physicalExposure = context.Get<float>("PhysicalExposure", 1.0f);
        float exposureComp = context.Get<float>("ExposureCompensation", 1.0f);
        m_PostProcessShader->SetFloat("u_Exposure", physicalExposure * exposureComp);
        
        int toneMappingType = context.Get<int>("ToneMappingType", 1);
        m_PostProcessShader->SetInt("u_ToneMappingType", toneMappingType);

        glm::vec2 texelSize = {
            1.0f / (float)m_PostProcessFBO->GetSpecification().Width,
            1.0f / (float)m_PostProcessFBO->GetSpecification().Height
        };
        m_PostProcessShader->SetFloat2("u_TexelSize", texelSize);

        // Bloom 合成
        bool enableBloom = bloomTexID != 0;
        m_PostProcessShader->SetBool("u_EnableBloom", enableBloom);
        if (enableBloom) {
            float bloomIntensity = context.Get<float>("BloomIntensity", 1.0f);
            m_PostProcessShader->SetFloat("u_BloomIntensity", bloomIntensity);
            
            m_PostProcessShader->SetInt("u_BloomTexture", 2); 
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, bloomTexID);
        }

        glBindVertexArray(m_EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        context.Stats.DrawCalls++; context.Stats.TriangleCount += 1; context.Stats.VertexCount += 3;
        
        m_PostProcessFBO->Unbind();

        // 最终结果写回黑板，供 FXAAPass 读取！
        context.Set("PostProcess_Output", m_PostProcessFBO->GetColorAttachmentRendererID(0));

        // 【新增】：将 FBO 实体也挂载到黑板，供高清截图读取像素！
        context.Framebuffers["PostProcess"] = m_PostProcessFBO;
    }
}