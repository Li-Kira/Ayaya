#include "ayapch.h"
#include "BloomPass.hpp"

namespace Ayaya {

    BloomPass::BloomPass() { 
        m_PassName = "Bloom Pass"; 
    }

    void BloomPass::OnAttach() {
        // 创建用于全屏绘制的空 VAO
        m_EmptyVAO = VertexArray::Create();
        
        m_DownsampleShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/bloom_downsample.frag");
        m_UpsampleShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/bloom_upsample.frag");

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
        downSpec.BackfaceCulling = CullMode::None; // 【核心】：全屏三角形必须关闭剔除！
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
        upSpec.BlendMode = BlendModeType::Additive; // 升采样使用 Additive (叠加) 混合发光
        upSpec.BackfaceCulling = CullMode::None; // 【核心】：全屏三角形必须关闭剔除！
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
        // 1. 获取输入：全面采用对象指针提取 Framebuffer
        // ==========================================
        std::shared_ptr<Framebuffer> inputFBO;
        if (context.Framebuffers.find("Lighting") != context.Framebuffers.end()) {
            inputFBO = context.Framebuffers["Lighting"];
        } else {
            inputFBO = context.Get<std::shared_ptr<Framebuffer>>("Lighting_Output", nullptr);
        }

        // 获取 Bloom 相关的参数，注意用正确的内置类型
        bool isBloomActive = context.Get<bool>("EnableBloom", true);
        float bloomRadius = context.Get<float>("BloomRadius", 0.005f); 
        float bloomThreshold = context.Get<float>("BloomThreshold", 1.0f); // 亮度大于 1.0 才发光
        float bloomKnee = context.Get<float>("BloomKnee", 0.1f);           // 软过渡范围

        // 如果没有输入，或者关掉了 Bloom，直接交接棒给下一关
        if (!inputFBO || !isBloomActive) {
            if (inputFBO) {
                context.Set("Bloom_Output", std::dynamic_pointer_cast<void>(inputFBO));
            } else {
                context.Set("Bloom_Output", std::shared_ptr<void>(nullptr));
            }
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

        std::shared_ptr<Framebuffer> currentInputFBO = inputFBO;

        for (size_t i = 0; i < m_MipChain.size(); i++) {
            auto& mip = m_MipChain[i];

            // 开始 Pass (不清屏，直接全屏覆盖)
            cmd.BeginRenderPass(mip.FBO, false, glm::vec4(0.0f)); 

            // 【现代绑定接口】：直接传 Framebuffer 对象！
            cmd.BindTexture2D(m_DownsamplePipeline, "u_Image", 0, currentInputFBO, 0);
            
            // 【数学修正】：完美使用当前 Mip 级别的倒数作为 TexelSize
            glm::vec2 texelSize = 1.0f / mip.Size; 
            cmd.PushConstant(m_DownsamplePipeline, "u_TexelSize", texelSize);

            cmd.PushConstant(m_DownsamplePipeline, "u_MipLevel", (int)i);

            if (context.RecordAndCheckDrawCall("Bloom Pass", "Downsample Mip " + std::to_string(i), "Bloom Shader", 1)) {
                cmd.DrawArrays(3); // 【修改】：抛弃 m_EmptyVAO
            }

            cmd.EndRenderPass();

            // 把这级画好的 FBO 对象，作为下一级降采样的输入
            currentInputFBO = mip.FBO;
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

            // 【现代绑定接口】：把上一级更模糊的 Mip 对象拿来放大
            cmd.BindTexture2D(m_UpsamplePipeline, "u_Image", 0, prevMip.FBO, 0);

            if (context.RecordAndCheckDrawCall("Bloom Pass", "Upsample Mip " + std::to_string(i), "Bloom Shader", 1)) {
                cmd.DrawArrays(3); // 【修改】：抛弃 m_EmptyVAO
            }

            cmd.EndRenderPass();
        }

        // ==========================================
        // 3. 输出交接：将最高清的那张模糊图 FBO 对象贴在黑板上
        // ==========================================
        context.Set("Bloom_Output", std::dynamic_pointer_cast<void>(m_MipChain[0].FBO));
        context.Framebuffers["Bloom"] = m_MipChain[0].FBO;
    }

}