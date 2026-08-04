#include "ayapch.h"
#include "VulkanApplyReflectionPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/TextureCube.hpp"

namespace Ayaya {

    VulkanApplyReflectionPass::VulkanApplyReflectionPass() { m_PassName = "Apply Reflection"; }

    void VulkanApplyReflectionPass::DeclareResources(RGBuilder& builder,
                                                      uint32_t width, uint32_t height) {
        builder.ReadTexture("SSR_Result");
        builder.ReadTexture("GBuffer");
        builder.ReadTexture("SceneDepth");
        builder.ReadTexture("SSAO_Final");

        FramebufferSpecification s;
        s.Width = width; s.Height = height; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        builder.WriteTexture("Lighting", s, AttachmentLoadOp::Load);
    }

    void VulkanApplyReflectionPass::OnAttach() {
        m_Shader = Shader::Create("SSR/apply_reflection.vert", "SSR/apply_reflection.frag");
        if (!m_Shader) {
            AYAYA_CORE_ERROR("[ApplyReflection] Failed to create shader!");
            return;
        }

        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        m_RefFBO = Framebuffer::Create(ref);

        PipelineSpecification ps;
        ps.Shader = m_Shader;
        ps.TargetFramebuffer = m_RefFBO;
        ps.Layout = {};
        ps.Topology = PrimitiveTopology::TriangleStrip;
        ps.DepthTest = false;
        ps.DepthWrite = false;
        // Additive blend: SSR/IBL specular adds onto Lighting_NoSpecIBL
        ps.Blend = true;
        ps.BlendMode = BlendModeType::Additive;  // One / One
        ps.BackfaceCulling = CullMode::None;
        m_Pipeline = Pipeline::Create(ps);
        if (!m_Pipeline)
            AYAYA_CORE_ERROR("[ApplyReflection] Failed to create pipeline!");
    }

    void VulkanApplyReflectionPass::Execute(RenderContext& context,
                                             RenderCommandBuffer& cmd) {
        // NEVER culled — this pass provides IBL specular even when SSR is disabled.
        // When SSR is off, ssrFBO is null → we bind BlackTexture → ssr=(0,0,0,0)
        // → ssrWeight=0 → pure IBL cubemap specular fallback.

        auto lightingFBO   = context.GetFramebuffer("Lighting");
        auto ssrFBO        = context.GetFramebuffer("SSR_Result");
        auto gbufferFBO    = context.GetFramebuffer("GBuffer");
        auto sceneDepthFBO = context.GetFramebuffer("SceneDepth");
        auto ssaoFBO       = context.GetFramebuffer("SSAO_Final");
        if (!lightingFBO || !gbufferFBO || !sceneDepthFBO) return;
        // ssrFBO may be null when SSR pass is culled — handled below with BlackTexture fallback

        // ── IBL resources ──
        auto prefilterMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
        auto brdfLUT      = context.GetTexture("BRDFLUT");
        auto whiteTex = context.GetTexture("WhiteTexture");
        auto blackTex = context.GetTexture("BlackTexture");

        float envIntensity = context.Get<float>("EnvironmentIntensity", 1.0f);
        float roughnessStart = 0.3f;
        float roughnessEnd   = 0.6f;

        // LOAD mode: additive blend onto existing Lighting content
        cmd.BeginRenderPass(lightingFBO, false, glm::vec4(0.0f));
        cmd.BindPipeline(m_Pipeline);

        // Set 1, Binding 0: SSR_Result (or BlackTexture when SSR disabled)
        if (ssrFBO)
            cmd.BindTexture2D(m_Pipeline, "u_SSRResult",    0, ssrFBO, 0);
        else if (blackTex)
            cmd.BindTexture2D(m_Pipeline, "u_SSRResult",    0, blackTex);
        // Set 1, Binding 1-3: GBuffer
        cmd.BindTexture2D(m_Pipeline, "g_Albedo",       1, gbufferFBO, 1);
        cmd.BindTexture2D(m_Pipeline, "g_Normal",       2, gbufferFBO, 0);
        cmd.BindTexture2D(m_Pipeline, "g_PBR",          3, gbufferFBO, 2);
        // Set 1, Binding 4-5: SceneDepth + SSAO
        cmd.BindTexture2D(m_Pipeline, "u_DepthMap",     4, sceneDepthFBO, 0, true);
        if (ssaoFBO)
            cmd.BindTexture2D(m_Pipeline, "u_SSAO",     5, ssaoFBO, 0);
        else if (whiteTex)
            cmd.BindTexture2D(m_Pipeline, "u_SSAO",     5, whiteTex);
        // Set 1, Binding 6-7: IBL cubemap + BRDF LUT
        if (prefilterMap)
            cmd.BindTextureCube(m_Pipeline, "u_PrefilteredMap", 6, prefilterMap);
        if (brdfLUT)
            cmd.BindTexture2D(m_Pipeline, "u_BRDFLUT", 7, brdfLUT);

        struct alignas(16) PC {
            glm::mat4 InverseViewProj;    // 64  @ 0
            glm::vec3 CameraPosition;     // 12  @ 64
            float     EnvIntensity;       //  4  @ 76
            float     RoughnessStart;     //  4  @ 80
            float     RoughnessEnd;       //  4  @ 84
            float     _pad;               //  4  @ 88
        } pc;
        pc.InverseViewProj = glm::inverse(context.ProjectionMatrix * context.ViewMatrix);
        pc.CameraPosition  = context.CameraPosition;
        pc.EnvIntensity    = envIntensity;
        pc.RoughnessStart  = roughnessStart;
        pc.RoughnessEnd    = roughnessEnd;
        pc._pad = 0.0f;
        cmd.PushConstantData(m_Pipeline, &pc, sizeof pc);

        context.RecordAndCheckDrawCall("Apply Reflection", "Lighting", "apply_reflection", 1);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();

        context.Framebuffers["Lighting"] = lightingFBO;
    }

} // namespace Ayaya
