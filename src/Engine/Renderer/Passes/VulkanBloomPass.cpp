#include "ayapch.h"
#include "VulkanBloomPass.hpp"

namespace Ayaya {

    VulkanBloomPass::VulkanBloomPass() {
        m_PassName = "Bloom Pass";
    }

    void VulkanBloomPass::OnAttach() {
        m_DownsampleShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/bloom_downsample.frag");
        m_UpsampleShader = Shader::Create("PostProcess/postprocess.vert", "PostProcess/bloom_upsample.frag");

        OnResize(1280, 720);

        PipelineSpecification downSpec;
        downSpec.Shader = m_DownsampleShader;
        downSpec.TargetFramebuffer = m_MipChain[0].FBO;
        downSpec.Layout = {};
        downSpec.DepthTest = false;
        downSpec.DepthWrite = false;
        downSpec.Blend = false;
        downSpec.BackfaceCulling = CullMode::None;
        m_DownsamplePipeline = Pipeline::Create(downSpec);

        PipelineSpecification upSpec;
        upSpec.Shader = m_UpsampleShader;
        upSpec.TargetFramebuffer = m_MipChain[0].FBO;
        upSpec.Layout = {};
        upSpec.DepthTest = false;
        upSpec.DepthWrite = false;
        upSpec.Blend = true;
        upSpec.BlendMode = BlendModeType::Additive;
        upSpec.BackfaceCulling = CullMode::None;
        m_UpsamplePipeline = Pipeline::Create(upSpec);
    }

    void VulkanBloomPass::OnResize(uint32_t width, uint32_t height) {
        m_MipChain.clear();

        glm::vec2 mipSize((float)width / 2.0f, (float)height / 2.0f);
        glm::vec2 intMipSize((float)(uint32_t)mipSize.x, (float)(uint32_t)mipSize.y);

        for (uint32_t i = 0; i < 5; i++) {
            VulkanBloomMip mip;
            mip.Size = mipSize;
            mip.IntSize = intMipSize;

            FramebufferSpecification spec;
            spec.Width = (uint32_t)intMipSize.x;
            spec.Height = (uint32_t)intMipSize.y;
            spec.Samples = 1;
            spec.Attachments = { FramebufferTextureFormat::RGBA16F };
            mip.FBO = Framebuffer::Create(spec);

            m_MipChain.push_back(mip);

            mipSize *= 0.5f;
            intMipSize = glm::vec2((float)(uint32_t)mipSize.x, (float)(uint32_t)mipSize.y);
        }
    }

    void VulkanBloomPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        std::shared_ptr<Framebuffer> inputFBO;
        if (context.Framebuffers.find("Lighting") != context.Framebuffers.end()) {
            inputFBO = context.Framebuffers["Lighting"];
        } else {
            inputFBO = context.Get<std::shared_ptr<Framebuffer>>("Lighting_Output", nullptr);
        }

        bool isBloomActive = context.Get<bool>("EnableBloom", true);
        float bloomRadius = context.Get<float>("BloomRadius", 0.005f);
        float bloomThreshold = context.Get<float>("BloomThreshold", 1.0f);
        float bloomKnee = context.Get<float>("BloomKnee", 0.1f);

        if (!inputFBO || !isBloomActive) {
            if (inputFBO) {
                context.Set("Bloom_Output", std::dynamic_pointer_cast<void>(inputFBO));
            } else {
                context.Set("Bloom_Output", std::shared_ptr<void>(nullptr));
            }
            return;
        }

        cmd.BindPipeline(m_DownsamplePipeline);
        context.Stats.ShaderBinds++;

        glm::vec3 curve(bloomThreshold - bloomKnee, bloomKnee * 2.0f, 0.25f / bloomKnee);

        std::shared_ptr<Framebuffer> currentInputFBO = inputFBO;

        for (size_t i = 0; i < m_MipChain.size(); i++) {
            auto& mip = m_MipChain[i];

            cmd.BeginRenderPass(mip.FBO, false, glm::vec4(0.0f));

            cmd.BindTexture2D(m_DownsamplePipeline, "u_Image", 0, currentInputFBO, 0);

            BloomDownsamplePushConstants constants{};
            constants.TexelSize = 1.0f / mip.Size;
            constants.MipLevel = (int)i;
            constants.Threshold = bloomThreshold;
            constants.Curve = curve;
            cmd.PushConstantData(m_DownsamplePipeline, &constants, sizeof(BloomDownsamplePushConstants));

            if (context.RecordAndCheckDrawCall("Bloom Pass", "Downsample Mip " + std::to_string(i), "Bloom Shader", 1)) {
                cmd.DrawArrays(3);
            }

            cmd.EndRenderPass();

            currentInputFBO = mip.FBO;
        }

        cmd.BindPipeline(m_UpsamplePipeline);
        context.Stats.ShaderBinds++;

        BloomUpsamplePushConstants upConstants{};
        upConstants.FilterRadius = bloomRadius;
        cmd.PushConstantData(m_UpsamplePipeline, &upConstants, sizeof(BloomUpsamplePushConstants));

        for (int i = (int)m_MipChain.size() - 2; i >= 0; i--) {
            auto& currentMip = m_MipChain[i];
            auto& prevMip = m_MipChain[i + 1];

            cmd.BeginRenderPass(currentMip.FBO, false, glm::vec4(0.0f));

            cmd.BindTexture2D(m_UpsamplePipeline, "u_Image", 0, prevMip.FBO, 0);

            if (context.RecordAndCheckDrawCall("Bloom Pass", "Upsample Mip " + std::to_string(i), "Bloom Shader", 1)) {
                cmd.DrawArrays(3);
            }

            cmd.EndRenderPass();
        }

        context.Set("Bloom_Output", std::dynamic_pointer_cast<void>(m_MipChain[0].FBO));
        context.Framebuffers["Bloom"] = m_MipChain[0].FBO;
    }

}
