#include "ayapch.h"
#include "VulkanLightingPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/TextureCube.hpp"
#include "Asset/AssetManager.hpp"

namespace Ayaya {

    void VulkanLightingPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        builder.ReadTexture("GBuffer");
        builder.ReadTexture("ShadowMap");
        FramebufferSpecification s;
        s.Width = width; s.Height = height; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        builder.WriteTexture("Lighting", s);
    }

    void VulkanLightingPass::OnAttach() {
        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        m_RefFBO = Framebuffer::Create(ref);

        m_DeferredShader = Shader::Create("Deferred/deferred_lighting.vert", "Deferred/deferred_lighting.frag");
        m_DeferredPipeSpec.Shader = m_DeferredShader; m_DeferredPipeSpec.Layout = {};
        m_DeferredPipeSpec.TargetFramebuffer = m_RefFBO;
        m_DeferredPipeSpec.DepthTest = true; m_DeferredPipeSpec.DepthWrite = true; // writes gl_FragDepth
        m_DeferredPipeSpec.Blend = false; m_DeferredPipeSpec.BackfaceCulling = CullMode::None;
        m_DeferredPipeline = Pipeline::Create(m_DeferredPipeSpec);
    }

    void VulkanLightingPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto gbufferFBO = context.GetFramebuffer("GBuffer");
        auto lightingFBO = context.GetFramebuffer("Lighting");
        if (!gbufferFBO || !lightingFBO) return;

        cmd.BeginRenderPass(lightingFBO, true, glm::vec4(0.0f));

        // PBR Deferred Shading
        cmd.BindPipeline(m_DeferredPipeline);
        cmd.BindTexture2D(m_DeferredPipeline, "u_DepthMap",   0, gbufferFBO, 0, true);  // TEST: hw depth at slot 0
        cmd.BindTexture2D(m_DeferredPipeline, "g_Albedo",     1, gbufferFBO, 1);
        cmd.BindTexture2D(m_DeferredPipeline, "g_PBR",        2, gbufferFBO, 2);
        cmd.BindTexture2D(m_DeferredPipeline, "g_CustomData", 3, gbufferFBO, 3);
        cmd.BindTexture2D(m_DeferredPipeline, "g_Normal",     4, gbufferFBO, 0);        // TEST: normal at slot 4

        auto irrMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap");
        auto preMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
        cmd.BindTextureCube(m_DeferredPipeline, "u_IrradianceMap", 8, irrMap);
        cmd.BindTextureCube(m_DeferredPipeline, "u_PrefilteredMap", 9, preMap);
        auto brdf = context.GetTexture("BRDFLUT");
        auto whiteTex = context.GetTexture("WhiteTexture");
        if (brdf) cmd.BindTexture2D(m_DeferredPipeline, "u_BRDFLUT", 10, brdf);
        else if (whiteTex) cmd.BindTexture2D(m_DeferredPipeline, "u_BRDFLUT", 10, whiteTex);

        auto shadowFBO = context.GetFramebuffer("ShadowMap");
        if (shadowFBO)
            cmd.BindTexture2D(m_DeferredPipeline, "u_ShadowMap", 5, shadowFBO, 0, true);
        else if (whiteTex)
            cmd.BindTexture2D(m_DeferredPipeline, "u_ShadowMap", 5, whiteTex);

        // SSAO (half-res R8)
        bool enableSSAO = context.Get<bool>("EnableSSAO", false);
        auto ssaoFBO = context.GetFramebuffer("SSAO_Final");
        if (enableSSAO && ssaoFBO)
            cmd.BindTexture2D(m_DeferredPipeline, "u_SSAO", 11, ssaoFBO, 0);
        else if (whiteTex)
            cmd.BindTexture2D(m_DeferredPipeline, "u_SSAO", 11, whiteTex);

        DeferredLightingPushConstants defPC{};
        defPC.LightSpaceMatrix = context.Get<glm::mat4>("LightSpaceMatrix", glm::mat4(1.0f));
        defPC.AmbientColor = context.Get<glm::vec3>("EnvironmentAmbientColor", glm::vec3(0.1f));
        defPC.Intensity = context.Get<float>("EnvironmentIntensity", 1.0f);
        defPC.EnvMapEnabled = (irrMap && preMap) ? 1 : 0;
        defPC.EnableSSAO = (enableSSAO && ssaoFBO != nullptr) ? 1 : 0;
        defPC.InverseViewProj = glm::inverse(context.ProjectionMatrix * context.ViewMatrix);
        static_assert(sizeof(DeferredLightingPushConstants) == 160, "PC size mismatch with GLSL");
        cmd.PushConstantData(m_DeferredPipeline, &defPC, sizeof(DeferredLightingPushConstants));
        cmd.DrawArrays(3);

        cmd.EndRenderPass();
        context.Framebuffers["Lighting"] = lightingFBO;
    }
}
