#include "ayapch.h"
#include "PostProcessPass.hpp"

namespace Ayaya {

    PostProcessPass::PostProcessPass() {
        m_PassName = "Post Process Pass";
    }

    void PostProcessPass::OnAttach() {
        m_EmptyVAO.reset(VertexArray::Create());

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

    void PostProcessPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t lightingTexID = context.Get<uint32_t>("Lighting_Output", 0);
        uint32_t bloomTexID = context.Get<uint32_t>("Bloom_Output", 0);

        if (lightingTexID == 0) return;

        m_PostProcessFBO->Bind();
        
        // 使用 cmd 控制管线状态
        cmd.SetViewport(0, 0, m_PostProcessFBO->GetSpecification().Width, m_PostProcessFBO->GetSpecification().Height);
        cmd.SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        cmd.Clear();
        cmd.SetDepthTest(false);

        m_PostProcessShader->Bind();
        context.Stats.ShaderBinds++;

        m_PostProcessShader->SetInt("u_ScreenTexture", 0);
        cmd.BindTexture2D(0, lightingTexID);

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

        bool enableBloom = bloomTexID != 0;
        m_PostProcessShader->SetBool("u_EnableBloom", enableBloom);
        if (enableBloom) {
            float bloomIntensity = context.Get<float>("BloomIntensity", 1.0f);
            m_PostProcessShader->SetFloat("u_BloomIntensity", bloomIntensity);
            
            m_PostProcessShader->SetInt("u_BloomTexture", 2); 
            cmd.BindTexture2D(2, bloomTexID);
        }

        if (context.RecordAndCheckDrawCall("Post Process Pass", "Tone Mapping & Combine", "PostProcess Shader", 1)) {
            // 通过 cmd 发送绘制指令
            cmd.DrawArrays(m_EmptyVAO, 3);
        }
        
        m_PostProcessFBO->Unbind();
        context.Set("PostProcess_Output", m_PostProcessFBO->GetColorAttachmentRendererID(0));
    }
}