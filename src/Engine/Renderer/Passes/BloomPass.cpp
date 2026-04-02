#include "ayapch.h"
#include "BloomPass.hpp"

namespace Ayaya {

    BloomPass::BloomPass() { m_PassName = "Bloom Pass"; }

    void BloomPass::OnAttach() {
        m_EmptyVAO = VertexArray::Create();
        m_DownsampleShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/bloom_downsample.frag");
        m_UpsampleShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/bloom_upsample.frag");

        PipelineSpecification downSpec;
        downSpec.Shader = m_DownsampleShader;
        downSpec.DepthTest = false;
        downSpec.DepthWrite = false;
        downSpec.Blend = false;
        m_DownsamplePipeline = Pipeline::Create(downSpec);

        PipelineSpecification upSpec;
        upSpec.Shader = m_UpsampleShader;
        upSpec.DepthTest = false;
        upSpec.DepthWrite = false;
        upSpec.Blend = true;
        upSpec.BlendMode = BlendMode::Additive; 
        m_UpsamplePipeline = Pipeline::Create(upSpec);
    }

    void BloomPass::OnResize(uint32_t width, uint32_t height) {
        m_MipChain.clear();
        glm::vec2 mipSize((float)width / 2.0f, (float)height / 2.0f);
        glm::vec2 mipIntSize((uint32_t)mipSize.x, (uint32_t)mipSize.y);
        
        for (int i = 0; i < 6; i++) {
            BloomMip mip;
            mip.Size = mipSize; mip.IntSize = mipIntSize;
            
            FramebufferSpecification spec;
            spec.Samples = 1; 
            spec.Width = (uint32_t)mipIntSize.x; spec.Height = (uint32_t)mipIntSize.y;
            spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth }; 
            mip.FBO = Framebuffer::Create(spec);
            m_MipChain.push_back(mip);
            
            mipSize *= 0.5f; mipIntSize /= 2.0f;
        }
    }

    void BloomPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        bool enableBloom = context.Get<bool>("EnableBloom", true);
        if (!enableBloom || m_MipChain.empty()) {
            context.Set("Bloom_Output", (uint32_t)0); 
            return;
        }

        uint32_t inputTextureID = context.Get<uint32_t>("Lighting_Output", 0);
        if (inputTextureID == 0) return;

        float threshold = context.Get<float>("BloomThreshold", 1.0f);
        float knee = context.Get<float>("BloomKnee", 0.1f);
        if (knee < 0.0001f) knee = 0.0001f;
        
        glm::vec3 curve(threshold - knee, knee * 2.0f, 0.25f / knee);
        float bloomRadius = context.Get<float>("BloomRadius", 0.005f);
        
        // ==========================================
        // 1. 降采样 (Downsample)
        // ==========================================
        cmd.BindPipeline(m_DownsamplePipeline);
        context.Stats.ShaderBinds++;
        
        // 【消灭 Shader 调用】：删除了 m_DownsampleShader->SetInt("u_Image", 0);

        for (size_t i = 0; i < m_MipChain.size(); i++) {
            auto& mip = m_MipChain[i];
            
            // 每个 Mip 层级开启独立的 RenderPass (带清屏)
            cmd.BeginRenderPass(mip.FBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

            cmd.PushConstant(m_DownsamplePipeline, "u_MipLevel", (int)i);

            if (i == 0) {
                cmd.PushConstant(m_DownsamplePipeline, "u_Threshold", threshold);
                cmd.PushConstant(m_DownsamplePipeline, "u_Curve", curve);
                // 【自动化】：内部自带了 Shader 的 Uniform 更新
                cmd.BindTexture2D(m_DownsamplePipeline, "u_Image", 0, inputTextureID);
            } else {
                // 【自动化】：内部自带了 Shader 的 Uniform 更新
                cmd.BindTexture2D(m_DownsamplePipeline, "u_Image", 0, m_MipChain[i - 1].FBO->GetColorAttachmentRendererID(0));
            }

            glm::vec2 srcTexelSize = (i == 0) ? 
                glm::vec2(1.0f / context.Get<uint32_t>("ViewportWidth", 1280), 1.0f / context.Get<uint32_t>("ViewportHeight", 720)) : 
                glm::vec2(1.0f / m_MipChain[i - 1].Size.x, 1.0f / m_MipChain[i - 1].Size.y);
            cmd.PushConstant(m_DownsamplePipeline, "u_TexelSize", srcTexelSize);

            if (context.RecordAndCheckDrawCall("Bloom Pass", "Downsample Mip " + std::to_string(i), "Downsample Shader", 1)) {
                cmd.DrawArrays(m_EmptyVAO, 3);
            }

            cmd.EndRenderPass(); // 结束当前 Mip 的通道
        }

        // ==========================================
        // 2. 升采样与混合 (Upsample)
        // ==========================================
        cmd.BindPipeline(m_UpsamplePipeline);
        context.Stats.ShaderBinds++;
        
        // 【消灭 Shader 调用】：删除了 m_UpsampleShader->SetInt("u_Image", 0);
        cmd.PushConstant(m_UpsamplePipeline, "u_FilterRadius", bloomRadius);
        
        for (int i = (int)m_MipChain.size() - 2; i >= 0; i--) {
            auto& currentMip = m_MipChain[i];
            auto& prevMip = m_MipChain[i + 1];

            // 开启 RenderPass！注意 clear = false
            cmd.BeginRenderPass(currentMip.FBO, false);

            // 【终极替换】：使用完整签名的 BindTexture2D，替代原本旧版的裸写
            cmd.BindTexture2D(m_UpsamplePipeline, "u_Image", 0, prevMip.FBO->GetColorAttachmentRendererID(0));

            if (context.RecordAndCheckDrawCall("Bloom Pass", "Upsample Mip " + std::to_string(i), "Upsample Shader", 1)) {
                cmd.DrawArrays(m_EmptyVAO, 3);
            }

            cmd.EndRenderPass(); // 结束当前 Mip 的通道
        }

        context.Set("Bloom_Output", m_MipChain[0].FBO->GetColorAttachmentRendererID(0));
    }
}