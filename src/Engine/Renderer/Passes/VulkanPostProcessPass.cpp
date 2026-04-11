#include "ayapch.h"
#include "VulkanPostProcessPass.hpp"
#include <glm/glm.hpp>

namespace Ayaya {

    VulkanPostProcessPass::VulkanPostProcessPass() {
        m_PassName = "Post Process Pass";
    }

    void VulkanPostProcessPass::OnAttach() {
        m_EmptyVAO = VertexArray::Create();

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
        pipelineSpec.DepthTest = false;  
        pipelineSpec.DepthWrite = false; 
        pipelineSpec.Blend = false;      
        
        // 【核心修复】：彻底关闭背面剔除！确保无论顺逆时针，三角形绝对会被画出来！
        pipelineSpec.BackfaceCulling = CullMode::None; 

        m_Pipeline = Pipeline::Create(pipelineSpec);
    }

    void VulkanPostProcessPass::OnResize(uint32_t width, uint32_t height) {
        m_PostProcessFBO->Resize(width, height);
    }

    void VulkanPostProcessPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");
        if (width == 0 || height == 0) return;

        // 1. 同步离线画布尺寸
        if (m_PostProcessFBO->GetSpecification().Width != width || m_PostProcessFBO->GetSpecification().Height != height) {
            m_PostProcessFBO->Resize(width, height);
        }

        // 2. 开启录制！必须给清屏色，满足 Vulkan 的附件匹配原则
        cmd.BeginRenderPass(m_PostProcessFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        
        // 3. 绑定状态机
        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;

        // ==========================================
        // 4. 【核心重构】：绑定贴图依赖 (对象级传递)
        // 从黑板上拿到前置 Pass 画好的 Framebuffer 对象
        // ==========================================
        std::shared_ptr<Framebuffer> inputFBO;

        // 找 VulkanClearPass 的测试底板
        if (context.Framebuffers.find("VulkanTarget") != context.Framebuffers.end()) {
            inputFBO = context.Framebuffers["VulkanTarget"];
        }

       if (inputFBO) {
            // 参数说明：管线, 变量名, 槽位(Slot), Framebuffer对象, 附件索引(Index)
            // 附件 0 通常是 Color / Albedo / HDR Output
            cmd.BindTexture2D(m_Pipeline, "u_ScreenTexture", 0, inputFBO, 0);
            
            // 为了填饱 Vulkan 严格的验证层（Shader 里声明了 3 个，必须给 3 个）
            // 我们暂时把同一张图塞进描边和 Bloom 的槽位里占位
            // cmd.BindTexture2D(m_Pipeline, "u_SelectionTexture", 1, inputFBO, 0);
            // cmd.BindTexture2D(m_Pipeline, "u_BloomTexture", 2, inputFBO, 0);
        }

        // ==========================================
        // 5. 参数注入 (Push Constants)
        // ==========================================
        float physExposure = context.Get<float>("PhysicalExposure", 1.0f);
        float expComp = context.Get<float>("ExposureCompensation", 1.0f);
        cmd.PushConstant(m_Pipeline, "u_Exposure", physExposure * expComp);
        
        int tmType = context.Get<int>("ToneMappingType", 1);
        cmd.PushConstant(m_Pipeline, "u_ToneMappingType", tmType);

        glm::vec2 texelSz = glm::vec2(1.0f / (float)width, 1.0f / (float)height);
        cmd.PushConstant(m_Pipeline, "u_TexelSize", texelSz);

        // 关闭 Bloom
        cmd.PushConstant(m_Pipeline, "u_EnableBloom", 0);
        cmd.PushConstant(m_Pipeline, "u_BloomIntensity", 0.0f); 

        // ==========================================
        // 6. 提交绘制！
        // ==========================================
        if (context.RecordAndCheckDrawCall("Post Process Pass", "Tone Mapping & Combine", "PostProcess Shader", 1)) {
            // 不传 VAO，只传顶点数 3，完美触发 vkCmdDraw
            cmd.DrawArrays(3); 
        }

        cmd.EndRenderPass();

        // ==========================================
        // 7. 【核心重构】：交还数据
        // 直接把 FBO 的智能指针挂到黑板上！
        // ImGui 或者后续的 FXAA 就可以直接拿着这个对象去做 vkUpdateDescriptorSets
        // ==========================================
        // 将自己的画布对象登记到全局字典里，这样 FXAA 就能去提取它了！
        context.Framebuffers["PostProcess"] = m_PostProcessFBO;
        context.Set("PostProcess_Output", std::dynamic_pointer_cast<void>(m_PostProcessFBO));

        // 作为保底，也写一份 Final_Output（防 FXAA 没开的情况）
        context.Set("Final_Output", std::dynamic_pointer_cast<void>(m_PostProcessFBO));
        // 注意：因为 context 存的是 std::any 或者 void*，这里用 shared_ptr<void> 或 shared_ptr<Framebuffer> 都可以，
        // 只要和获取的地方 (EditorLayer) 匹配即可。为了安全，推荐黑板数据类型统一存 shared_ptr<Framebuffer>。
    }
}