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
        m_FXAAShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/fxaa.frag");

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

        // 【核心修复】：彻底关闭背面剔除，防止全屏三角形被吃掉！
        // pipelineSpec.BackfaceCulling = CullMode::None;

        m_Pipeline = Pipeline::Create(pipelineSpec);
    }

    void FXAAPass::OnResize(uint32_t width, uint32_t height) {
        m_FXAAFBO->Resize(width, height);
    }

    void FXAAPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        // ==========================================
        // 1. 获取输入：直接获取 PostProcess 传过来的 Framebuffer 对象！
        // ==========================================
        std::shared_ptr<Framebuffer> inputFBO;
        
        // 尝试从字典里或者黑板上拿到上一阶段的 FBO
        if (context.Framebuffers.find("PostProcess") != context.Framebuffers.end()) {
            inputFBO = context.Framebuffers["PostProcess"];
        } else {
            inputFBO = context.Get<std::shared_ptr<Framebuffer>>("PostProcess_Output", nullptr);
        }
        
        // 2. 检查 FXAA 是否在 UI 中被开启
        bool isFXAAActive = context.Get<bool>("EnableFXAA", true);

        // 如果没有输入图，或者 FXAA 被关闭，直接把输入的对象当作最终结果传出去！
        if (!inputFBO || !isFXAAActive) {
            if (inputFBO) {
                context.Set("Final_Output", std::dynamic_pointer_cast<void>(inputFBO));
            }
            return;
        }

        // 3. 开始执行 FXAA 渲染
        cmd.BeginRenderPass(m_FXAAFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        
        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;
        
        // ==========================================
        // 【核心重构】：绑定贴图，直接传入 FBO 对象！
        // ==========================================
        cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, inputFBO, 0);
        
        glm::vec2 texelSz = glm::vec2(
            1.0f / (float)m_FXAAFBO->GetSpecification().Width,
            1.0f / (float)m_FXAAFBO->GetSpecification().Height
        );
        cmd.PushConstant(m_Pipeline, "u_TexelSize", texelSz);

        // 执行绘制
        if (context.RecordAndCheckDrawCall("FXAA Pass", "Anti-Aliasing", "FXAA Shader", 1)) {
            cmd.DrawArrays(3);
        }
        
        cmd.EndRenderPass();

        // 4. 将抗锯齿后的 FBO 对象，写回黑板，供 EditorLayer 视口读取！
        context.Set("Final_Output", std::dynamic_pointer_cast<void>(m_FXAAFBO));
        context.Framebuffers["FXAA"] = m_FXAAFBO;
    }

}