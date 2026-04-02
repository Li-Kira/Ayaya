#include "ayapch.h"
#include "PostProcessPass.hpp"
#include <glm/glm.hpp>

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

        // ==========================================
        // 【核心新增】：把渲染状态前置打包为管线图纸 (PSO)
        // ==========================================
        PipelineSpecification pipelineSpec;
        pipelineSpec.Shader = m_PostProcessShader;
        pipelineSpec.TargetFramebuffer = m_PostProcessFBO;
        pipelineSpec.DepthTest = false;  // 后处理画全屏三角形，不需要深度测试
        pipelineSpec.DepthWrite = false; // 不需要写深度
        pipelineSpec.Blend = false;      // 直接覆盖像素，不需要开启混合

        m_Pipeline = Pipeline::Create(pipelineSpec);
    }

    void PostProcessPass::OnResize(uint32_t width, uint32_t height) {
        m_PostProcessFBO->Resize(width, height);
    }

    void PostProcessPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        // 提取黑板上的 3 张关键贴图
        uint32_t lightingTexID = context.Get<uint32_t>("Lighting_Output", 0);
        uint32_t selectionTexID = context.Get<uint32_t>("Selection_Output", 0);
        uint32_t bloomTexID = context.Get<uint32_t>("Bloom_Output", 0);
        
        if (lightingTexID == 0) return;

        cmd.BeginRenderPass(m_PostProcessFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        
        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;


        // 1. 基础贴图 (Slot 0)
        m_PostProcessShader->SetInt("u_ScreenTexture", 0);
        cmd.BindTexture2D(0, lightingTexID);

        // 2. 选择轮廓掩码 (Slot 1)
        m_PostProcessShader->SetInt("u_SelectionTexture", 1);
        cmd.BindTexture2D(1, selectionTexID);

        // 参数注入
        float physExposure = context.Get<float>("PhysicalExposure", 1.0f);
        float expComp = context.Get<float>("ExposureCompensation", 1.0f);
        m_PostProcessShader->SetFloat("u_Exposure", physExposure * expComp);
        
        int tmType = context.Get<int>("ToneMappingType", 1);
        m_PostProcessShader->SetInt("u_ToneMappingType", tmType);

        glm::vec2 texelSz = glm::vec2(
            1.0f / (float)m_PostProcessFBO->GetSpecification().Width,
            1.0f / (float)m_PostProcessFBO->GetSpecification().Height
        );
        m_PostProcessShader->SetFloat2("u_TexelSize", texelSz);

        // 3. Bloom 合成 (Slot 2)
        bool isBloomEnabled = (bloomTexID != 0);
        m_PostProcessShader->SetBool("u_EnableBloom", isBloomEnabled);
        if (isBloomEnabled) {
            float bloomInt = context.Get<float>("BloomIntensity", 1.0f);
            m_PostProcessShader->SetFloat("u_BloomIntensity", bloomInt);
            
            m_PostProcessShader->SetInt("u_BloomTexture", 2); 
            cmd.BindTexture2D(2, bloomTexID);
        }

        if (context.RecordAndCheckDrawCall("Post Process Pass", "Tone Mapping & Combine", "PostProcess Shader", 1)) {
            cmd.DrawArrays(m_EmptyVAO, 3);
        }
        
        cmd.EndRenderPass();

        // 最终结果写回黑板，供 FXAAPass 读取！
        context.Set("PostProcess_Output", m_PostProcessFBO->GetColorAttachmentRendererID(0));
        context.Framebuffers["PostProcess"] = m_PostProcessFBO;
    }
}