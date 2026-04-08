#include "ayapch.h"
#include "BloomPass.hpp"

namespace Ayaya {

    BloomPass::BloomPass() { 
        m_PassName = "Bloom Pass"; 
    }

    void BloomPass::OnAttach() {
        // 创建用于全屏绘制的空 VAO
        m_EmptyVAO = VertexArray::Create();
        
        m_DownsampleShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/bloom_downsample.frag");
        m_UpsampleShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/bloom_upsample.frag");

        // ==========================================
        // 初始化尺寸：为了让下面的 Pipeline 能拿到格式，必须先生成一次 MipChain
        // ==========================================
        OnResize(1280, 720); 

        // ==========================================
        // 固化降采样管线 (覆盖混合)
        // ==========================================
        PipelineSpecification downSpec;
        downSpec.Shader = m_DownsampleShader;
        downSpec.TargetFramebuffer = m_MipChain[0].FBO; // 关键：指定 HDR 目标格式
        downSpec.DepthTest = false;
        downSpec.DepthWrite = false;
        downSpec.Blend = false; // 降采样直接覆盖
        m_DownsamplePipeline = Pipeline::Create(downSpec);

        // ==========================================
        // 固化升采样管线 (叠加混合)
        // ==========================================
        PipelineSpecification upSpec;
        upSpec.Shader = m_UpsampleShader;
        upSpec.TargetFramebuffer = m_MipChain[0].FBO; // 关键：指定 HDR 目标格式
        upSpec.DepthTest = false;
        upSpec.DepthWrite = false;
        upSpec.Blend = true;
        upSpec.BlendMode = BlendMode::Additive; // 升采样使用 Additive (叠加) 混合发光
        m_UpsamplePipeline = Pipeline::Create(upSpec);
    }

    void BloomPass::OnResize(uint32_t width, uint32_t height) {
        m_MipChain.clear();
        
        // Bloom 从屏幕尺寸的一半开始降采样
        glm::vec2 mipSize((float)width / 2.0f, (float)height / 2.0f);
        glm::vec2 intMipSize((float)(uint32_t)mipSize.x, (float)(uint32_t)mipSize.y);
        
        // 生成 5 级 Mipmap 链
        for (uint32_t i = 0; i < 5; i++) {
            BloomMip mip;
            mip.Size = mipSize;
            mip.IntSize = intMipSize;
            
            FramebufferSpecification spec;
            spec.Width = (uint32_t)intMipSize.x;
            spec.Height = (uint32_t)intMipSize.y;
            spec.Samples = 1;
            spec.Attachments = { FramebufferTextureFormat::RGBA16F }; // 必须使用浮点格式保存发光值
            mip.FBO = Framebuffer::Create(spec);
            
            m_MipChain.push_back(mip);
            
            // 每次宽高减半
            mipSize *= 0.5f;
            intMipSize = glm::vec2((float)(uint32_t)mipSize.x, (float)(uint32_t)mipSize.y);
        }
    }

    void BloomPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        // ==========================================
        // 1. 获取输入：严格使用 void* 取出并强转，防止崩溃！
        // ==========================================
        void* rawInputTex = context.Get<void*>("Lighting_Output", nullptr);
        uint32_t inputTextureID = (uint32_t)(intptr_t)rawInputTex;

        // 获取 Bloom 相关的参数，注意用正确的内置类型
        bool isBloomActive = context.Get<bool>("EnableBloom", true);
        float bloomRadius = context.Get<float>("BloomRadius", 0.005f); 
        float bloomThreshold = context.Get<float>("BloomThreshold", 1.0f); // 亮度大于 1.0 才发光
        float bloomKnee = context.Get<float>("BloomKnee", 0.1f);           // 软过渡范围

        // 如果没有输入，或者关掉了 Bloom，直接交接棒给下一关
        if (inputTextureID == 0 || !isBloomActive) {
            context.Set("Bloom_Output", rawInputTex);
            return;
        }

        // ==========================================
        // 阶段 A: 降采样 (Downsample) 提取高光
        // ==========================================
        cmd.BindPipeline(m_DownsamplePipeline);
        context.Stats.ShaderBinds++;

        glm::vec3 curve(bloomThreshold - bloomKnee, bloomKnee * 2.0f, 0.25f / bloomKnee);
        cmd.PushConstant(m_DownsamplePipeline, "u_Threshold", bloomThreshold);
        cmd.PushConstant(m_DownsamplePipeline, "u_Curve", curve);

        uint32_t currentInputTexture = inputTextureID;

        for (size_t i = 0; i < m_MipChain.size(); i++) {
            auto& mip = m_MipChain[i];

            // 开始 Pass (不清屏，直接全屏覆盖)
            cmd.BeginRenderPass(mip.FBO, false, glm::vec4(0.0f)); 

            cmd.BindTexture2D(m_DownsamplePipeline, "u_Image", 0, currentInputTexture);
            
            // 【数学修正】：完美使用当前 Mip 级别的倒数作为 TexelSize
            glm::vec2 texelSize = 1.0f / mip.Size; 
            cmd.PushConstant(m_DownsamplePipeline, "u_TexelSize", texelSize);

            cmd.PushConstant(m_DownsamplePipeline, "u_MipLevel", (int)i);

            if (context.RecordAndCheckDrawCall("Bloom Pass", "Downsample Mip " + std::to_string(i), "Bloom Shader", 1)) {
                cmd.DrawArrays(m_EmptyVAO, 3);
            }

            cmd.EndRenderPass();

            // 把这级画好的 FBO，作为下一级降采样的输入
            // 加上强转：先把指针转为中立的 intptr_t，再转为 uint32_t 数字
            currentInputTexture = (uint32_t)(intptr_t)mip.FBO->GetColorAttachmentRendererID(0);
        }

        // ==========================================
        // 阶段 B: 升采样与混合 (Upsample) 扩散发光
        // ==========================================
        cmd.BindPipeline(m_UpsamplePipeline);
        context.Stats.ShaderBinds++;

        cmd.PushConstant(m_UpsamplePipeline, "u_FilterRadius", bloomRadius);

        // 从倒数第二小的那张图开始，一路放大并叠加回最大的那张图
        for (int i = (int)m_MipChain.size() - 2; i >= 0; i--) {
            auto& currentMip = m_MipChain[i];
            auto& prevMip = m_MipChain[i + 1];

            cmd.BeginRenderPass(currentMip.FBO, false, glm::vec4(0.0f));

            // 把上一级更模糊的 Mip 拿来放大
            uint32_t prevTexture = (uint32_t)(intptr_t)prevMip.FBO->GetColorAttachmentRendererID(0);
            cmd.BindTexture2D(m_UpsamplePipeline, "u_Image", 0, prevTexture);

            if (context.RecordAndCheckDrawCall("Bloom Pass", "Upsample Mip " + std::to_string(i), "Bloom Shader", 1)) {
                cmd.DrawArrays(m_EmptyVAO, 3);
            }

            cmd.EndRenderPass();
        }

        // ==========================================
        // 3. 输出交接：将最高清的那张模糊图转为 void* 贴在黑板上
        // ==========================================
        uint32_t finalBloomTexture = (uint32_t)(intptr_t)m_MipChain[0].FBO->GetColorAttachmentRendererID(0);
        context.Set("Bloom_Output", (void*)(intptr_t)finalBloomTexture);
    }

}