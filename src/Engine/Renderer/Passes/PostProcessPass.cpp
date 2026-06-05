#include "ayapch.h"
#include "PostProcessPass.hpp"
#include <glm/glm.hpp>

namespace Ayaya {

    PostProcessPass::PostProcessPass() {
        m_PassName = "Post Process Pass (OpenGL)";
    }

    void PostProcessPass::OnAttach() {
        m_EmptyVAO = VertexArray::Create();

        m_PostProcessShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/postprocess.frag");

        FramebufferSpecification postSpec;
        postSpec.Samples = 1; 
        postSpec.Width = 1280; postSpec.Height = 720;
        postSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_PostProcessFBO = Framebuffer::Create(postSpec);

        PipelineSpecification pipelineSpec;
        pipelineSpec.Shader = m_PostProcessShader;
        pipelineSpec.TargetFramebuffer = m_PostProcessFBO;
        pipelineSpec.DepthTest = false;  
        pipelineSpec.DepthWrite = false; 
        pipelineSpec.Blend = false;      
        pipelineSpec.BackfaceCulling = CullMode::None; 

        m_Pipeline = Pipeline::Create(pipelineSpec);
    }

    void PostProcessPass::OnResize(uint32_t width, uint32_t height) {
        m_PostProcessFBO->Resize(width, height);
    }

    void PostProcessPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");

        if (width == 0 || height == 0) return;

        if (m_PostProcessFBO->GetSpecification().Width != width || m_PostProcessFBO->GetSpecification().Height != height) {
            m_PostProcessFBO->Resize(width, height);
        }

        cmd.BeginRenderPass(m_PostProcessFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        
        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;

        // ==========================================
        // 1. 获取主画面输入对象 (Screen Texture)
        // 【防弹设计】：直接从强类型字典里拿！
        // ==========================================
        std::shared_ptr<Framebuffer> screenFBO;
        uint32_t screenAttachmentIndex = 0;
        
        if (context.Framebuffers.find("Lighting") != context.Framebuffers.end()) {
            screenFBO = context.Framebuffers["Lighting"];
            screenAttachmentIndex = 0;
        } else if (context.Framebuffers.find("GBuffer") != context.Framebuffers.end()) {
            screenFBO = context.Framebuffers["GBuffer"];
            screenAttachmentIndex = 3;
        }

        if (screenFBO) {
            cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, screenFBO, screenAttachmentIndex);
        }

        // ==========================================
        // 2. 获取描边掩码对象 (Selection Texture)
        // ==========================================
        std::shared_ptr<Framebuffer> selectionFBO;
        if (context.Framebuffers.find("Selection") != context.Framebuffers.end()) {
            selectionFBO = context.Framebuffers["Selection"];
        }

        if (selectionFBO) {
            cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, selectionFBO, 0);
        } else {
            // 【核心修复】：如果找不到描边 FBO，用全局的白模贴图占位，绝不能用主画面占位！
            auto whiteTex = context.GetTexture("WhiteTexture");
            if (whiteTex) {
                cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, whiteTex);
            }
        }

        // ==========================================
        // 参数注入
        // ==========================================
        float physExposure = context.Get<float>("PhysicalExposure", 1.0f);
        float expComp = context.Get<float>("ExposureCompensation", 1.0f);
        cmd.PushConstant(m_Pipeline, "u_Exposure", physExposure * expComp);
        
        int tmType = context.Get<int>("ToneMappingType", 1);
        cmd.PushConstant(m_Pipeline, "u_ToneMappingType", tmType);

        glm::vec2 texelSz = glm::vec2(
            1.0f / (float)m_PostProcessFBO->GetSpecification().Width,
            1.0f / (float)m_PostProcessFBO->GetSpecification().Height
        );
        cmd.PushConstant(m_Pipeline, "u_TexelSize", texelSz);

        // ==========================================
        // 3. 获取泛光对象 (Bloom Texture)
        // 【防弹设计】：彻底抛弃 context.Get，改用强类型字典查找
        // ==========================================
        std::shared_ptr<Framebuffer> bloomFBO;
        if (context.Framebuffers.find("Bloom") != context.Framebuffers.end()) {
            bloomFBO = context.Framebuffers["Bloom"];
        }

        bool isBloomEnabled = (bloomFBO != nullptr) && context.Get<bool>("EnableBloom", true);
        
        cmd.PushConstant(m_Pipeline, "u_EnableBloom", isBloomEnabled ? 1 : 0);
        if (isBloomEnabled) {
            float bloomInt = context.Get<float>("BloomIntensity", 1.0f);
            cmd.PushConstant(m_Pipeline, "u_BloomIntensity", bloomInt);
            cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, bloomFBO, 0);
        } else if (screenFBO) {
            cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, screenFBO, screenAttachmentIndex);
        }

        // ==========================================
        // 提交绘制
        // ==========================================
        if (context.RecordAndCheckDrawCall("Post Process Pass", "Tone Mapping & Combine", "PostProcess Shader", 1)) {
            cmd.DrawArrays(m_EmptyVAO, 3);
        }

        cmd.EndRenderPass();

        // ==========================================
        // 【核心交接】：将 FBO 实体指针交还给黑板
        // 【修复】：去掉了 dynamic_pointer_cast<void>！
        // 必须原汁原味地交出 Framebuffer 对象，否则 SceneRenderer 会崩溃！
        // ==========================================
        context.Set("Final_Output", m_PostProcessFBO);
        context.Set("PostProcess_Output", m_PostProcessFBO);
        context.Framebuffers["FinalOutput"] = m_PostProcessFBO;
        context.Framebuffers["FXAA"] = m_PostProcessFBO;
        context.Framebuffers["PostProcess"] = m_PostProcessFBO;
    }

}