#include "ayapch.h"
#include "VulkanSSRPass.hpp"
#include "VulkanGBufferPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Core/Application.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    // ── Blue Noise texture (64×64 RGBA8, shared across all SSR pass instances) ──
    static std::shared_ptr<Texture2D> s_BlueNoise;

    std::shared_ptr<Texture2D> VulkanSSRPass::GetBlueNoiseTexture() {
        if (!s_BlueNoise) {
            s_BlueNoise = Texture2D::Create(64, 64);
            if (s_BlueNoise) {
                // Fill with precomputed blue noise directions (hash-based pseudo-random)
                std::vector<uint32_t> noise(64 * 64);
                for (int y = 0; y < 64; y++) {
                    for (int x = 0; x < 64; x++) {
                        // Simple hash-based blue noise approximation
                        uint32_t h = uint32_t(x * 193 + y * 397 + (x ^ y) * 277) * 0x9E3779B9u;
                        h = (h ^ (h >> 16)) * 0x85EBCA6Bu;
                        h = h ^ (h >> 13);
                        // Pack as RGBA8: .rg = direction in [-1,1], .ba = 0
                        float angle = float(h) / float(0xFFFFFFFFu) * 6.2831853f;
                        uint8_t r = uint8_t((cos(angle) * 0.5f + 0.5f) * 255.0f);
                        uint8_t g = uint8_t((sin(angle) * 0.5f + 0.5f) * 255.0f);
                        noise[y * 64 + x] = r | (uint32_t(g) << 8) | (0u << 16) | (255u << 24);
                    }
                }
                s_BlueNoise->SetData(noise.data(), sizeof(uint32_t) * 64 * 64);
            }
        }
        return s_BlueNoise;
    }

    void VulkanSSRPass::ReleaseBlueNoiseTexture() {
        s_BlueNoise.reset();
    }

    VulkanSSRPass::VulkanSSRPass() { m_PassName = "SSR"; }

    VulkanSSRPass::~VulkanSSRPass() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        VkDevice device = vkCtx ? vkCtx->GetDevice() : VK_NULL_HANDLE;
        if (device) {
            if (m_HiZPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, m_HiZPool, nullptr);
                m_HiZPool = VK_NULL_HANDLE;
            }
            if (m_HiZSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, m_HiZSetLayout, nullptr);
                m_HiZSetLayout = VK_NULL_HANDLE;
            }
        }
    }

    void VulkanSSRPass::DeclareResources(RGBuilder& builder,
                                          uint32_t width, uint32_t height) {
        builder.ReadTexture("SceneDepth");
        builder.ReadTexture("GBuffer");
        builder.ReadTexture("Lighting");
        FramebufferSpecification s;
        s.Width = width / 2;  s.Height = height / 2;  s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F };
        builder.WriteTexture("SSR_Result", s);
    }

    void VulkanSSRPass::OnAttach() {
        m_MarchShader = Shader::Create("SSR/ssr_march.vert", "SSR/ssr_march.frag");
        if (!m_MarchShader) {
            AYAYA_CORE_ERROR("[SSRPass] Failed to create SSR march shader!");
            return;
        }

        // ── Hi-Z descriptor set layout (Set 2, Binding 0: sampler2D) ──
        {
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            VkDevice device = vkCtx ? vkCtx->GetDevice() : VK_NULL_HANDLE;
            if (device) {
                VkDescriptorSetLayoutBinding hizBinding{};
                hizBinding.binding = 0;
                hizBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                hizBinding.descriptorCount = 1;
                hizBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

                VkDescriptorSetLayoutCreateInfo layoutCI{};
                layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutCI.bindingCount = 1;
                layoutCI.pBindings = &hizBinding;
                vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &m_HiZSetLayout);

                VkDescriptorPoolSize poolSize{};
                poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                poolSize.descriptorCount = 3; // per frame-in-flight

                VkDescriptorPoolCreateInfo poolCI{};
                poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                poolCI.maxSets = 3;
                poolCI.poolSizeCount = 1;
                poolCI.pPoolSizes = &poolSize;
                vkCreateDescriptorPool(device, &poolCI, nullptr, &m_HiZPool);

                VkDescriptorSetLayout layouts[3] = { m_HiZSetLayout, m_HiZSetLayout, m_HiZSetLayout };
                VkDescriptorSetAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocInfo.descriptorPool = m_HiZPool;
                allocInfo.descriptorSetCount = 3;
                allocInfo.pSetLayouts = layouts;
                vkAllocateDescriptorSets(device, &allocInfo, m_HiZSets);
            }
        }

        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = { FramebufferTextureFormat::RGBA16F };
        m_RefFBO = Framebuffer::Create(ref);

        // Inject Hi-Z set layout before pipeline creation
        VulkanPipeline::s_ExtraSetLayouts.push_back(m_HiZSetLayout);
        PipelineSpecification ps;
        ps.Shader = m_MarchShader; ps.TargetFramebuffer = m_RefFBO; ps.Layout = {};
        ps.Topology = PrimitiveTopology::TriangleStrip;
        ps.DepthTest = false; ps.DepthWrite = false; ps.Blend = false;
        ps.BackfaceCulling = CullMode::None;
        m_MarchPipeline = Pipeline::Create(ps);
        VulkanPipeline::s_ExtraSetLayouts.clear();
        if (!m_MarchPipeline)
            AYAYA_CORE_ERROR("[SSRPass] Failed to create SSR march pipeline!");
    }

    void VulkanSSRPass::OnResize(uint32_t width, uint32_t height) {
        uint32_t hw = width / 2, hh = height / 2;
        if (hw == m_LastW && hh == m_LastH) return;
        m_LastW = hw; m_LastH = hh;
        FramebufferSpecification s;
        s.Width = hw; s.Height = hh; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F };
        m_ReflectionFBO = Framebuffer::Create(s);
    }

    void VulkanSSRPass::Execute(RenderContext& context,
                                 RenderCommandBuffer& cmd) {
        if (!context.Get<bool>("EnableSSR", false)) return;

        auto gbufferFBO    = context.GetFramebuffer("GBuffer");
        auto sceneDepthFBO = context.GetFramebuffer("SceneDepth");
        auto lightingFBO   = context.GetFramebuffer("Lighting");
        auto ssrFBO        = context.GetFramebuffer("SSR_Result");
        if (!gbufferFBO || !sceneDepthFBO || !lightingFBO || !ssrFBO) return;

        uint32_t vpW = context.Get<uint32_t>("ViewportWidth");
        uint32_t vpH = context.Get<uint32_t>("ViewportHeight");
        if (!vpW || !vpH) return;

        if (vpW / 2 != m_LastW || vpH / 2 != m_LastH)
            OnResize(vpW, vpH);
        if (!m_ReflectionFBO) return;

        // ── Ray March (half-res → RenderGraph-managed SSR_Result) ──
        // NOTE: No manual TransitionImageLayout — the RenderGraph handles
        // EnsureWritable (→COLOR_ATTACHMENT) pre-pass and
        // InsertTileResolveBarrier (→SHADER_READ_ONLY) post-pass automatically.
        cmd.BeginRenderPass(ssrFBO, true, glm::vec4(0.0f));
        cmd.BindPipeline(m_MarchPipeline);
        cmd.BindTexture2D(m_MarchPipeline, "u_DepthMap", 0, sceneDepthFBO, 0, true);
        cmd.BindTexture2D(m_MarchPipeline, "g_Normal",   4, gbufferFBO, 0);
        cmd.BindTexture2D(m_MarchPipeline, "g_Albedo",   1, gbufferFBO, 1);
        cmd.BindTexture2D(m_MarchPipeline, "g_PBR",      2, gbufferFBO, 2);
        cmd.BindTexture2D(m_MarchPipeline, "u_Lighting", 6, lightingFBO, 0);
        auto blueNoise = GetBlueNoiseTexture();
        if (blueNoise)
            cmd.BindTexture2D(m_MarchPipeline, "u_BlueNoise", 7, blueNoise);

        // ── Hi-Z binding (Set 2, Binding 0) for accelerated ray march ──
        // Always bind Hi-Z set: pipeline layout declares Set 2 (injected via s_ExtraSetLayouts),
        // so Vulkan requires a compatible descriptor set bound at draw time regardless of m_UseHiZ.
        // The shader won't access u_HiZ when HiZMipCount==0, but the binding must be present.
        if (m_GBufferPass && m_HiZSetLayout) {
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            if (vkCtx) {
                uint32_t fi = vkCtx->GetCurrentFrameIndex();
                uint32_t hizIdx = m_GBufferPass->GetCurrentHiZIndex();

                VkDescriptorImageInfo hizInfo{};
                hizInfo.sampler     = m_GBufferPass->GetHiZSampler();
                hizInfo.imageView   = m_GBufferPass->GetHiZImageView(hizIdx);
                hizInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkWriteDescriptorSet w{};
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet          = m_HiZSets[fi];
                w.dstBinding      = 0;
                w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.descriptorCount = 1;
                w.pImageInfo      = &hizInfo;
                vkUpdateDescriptorSets(vkCtx->GetDevice(), 1, &w, 0, nullptr);

                auto vkPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_MarchPipeline);
                if (vkPipe) {
                    vkCmdBindDescriptorSets(
                        vkCtx->GetCurrentCommandBuffer(),
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        vkPipe->GetVulkanPipelineLayout(),
                        2, 1, &m_HiZSets[fi], 0, nullptr);
                }
            }
        }

        struct alignas(16) SSR_PC {
            glm::mat4 InvProj;         // 64  @ 0
            glm::mat4 Proj;            // 64  @ 64
            glm::mat4 View;            // 64  @ 128
            float MaxSteps;            // 4   @ 192
            float StepSize;            // 4   @ 196
            float Thickness;           // 4   @ 200
            float EdgeFade;            // 4   @ 204
            int   MaxBinarySteps;      // 4   @ 208
            float RoughnessCutoff;     // 4   @ 212
            int   Enabled;             // 4   @ 216
            int   HiZMipCount;         // 4   @ 220
            int   FrameIndex;          // 4   @ 224
        } pc;
        pc.InvProj  = glm::inverse(context.ProjectionMatrix);
        pc.Proj     = context.ProjectionMatrix;
        pc.View     = context.ViewMatrix;
        pc.MaxSteps = context.Get<float>("SSR_MaxSteps", 128.0f);
        pc.StepSize = context.Get<float>("SSR_StepSize", 0.125f);
        pc.Thickness = context.Get<float>("SSR_Thickness", 0.3f);
        pc.EdgeFade = context.Get<float>("SSR_EdgeFade", 0.2f);
        pc.MaxBinarySteps = context.Get<int>("SSR_MaxBinarySteps", 8);
        pc.RoughnessCutoff = context.Get<float>("SSR_RoughnessCutoff", 1.0f);
        pc.Enabled = 1;
        pc.HiZMipCount = (m_UseHiZ && m_GBufferPass) ? int(m_GBufferPass->GetHiZMipCount()) : 0;
        static uint32_t s_SSRFrameCounter = 0;  // monotonic frame counter for temporal jitter
        pc.FrameIndex = int(s_SSRFrameCounter++);
        cmd.PushConstantData(m_MarchPipeline, &pc, sizeof pc);
        context.RecordAndCheckDrawCall("SSR", "SSR_Result", "ssr_march", 1);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();

        context.Framebuffers["SSR_Result"] = ssrFBO;
    }

} // namespace Ayaya
