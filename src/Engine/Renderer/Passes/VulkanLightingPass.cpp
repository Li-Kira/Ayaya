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

        m_SkyboxShader = Shader::Create("Skybox/skybox.vert", "Skybox/skybox.frag");
        m_SkyboxPipeSpec.Shader = m_SkyboxShader;
        m_SkyboxPipeSpec.Layout = {{ShaderDataType::Float3,"a_Position"},{ShaderDataType::Float3,"a_Normal"},{ShaderDataType::Float2,"a_TexCoord"},{ShaderDataType::Float3,"a_Tangent"}};
        m_SkyboxPipeSpec.TargetFramebuffer = m_RefFBO;
        m_SkyboxPipeSpec.DepthTest = true; m_SkyboxPipeSpec.DepthWrite = false;
        m_SkyboxPipeSpec.DepthOperator = DepthCompareOperator::LEqual;
        m_SkyboxPipeSpec.BackfaceCulling = CullMode::None;
        m_SkyboxPipeline = Pipeline::Create(m_SkyboxPipeSpec);
    }

    void VulkanLightingPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto gbufferFBO = context.GetFramebuffer("GBuffer");
        auto lightingFBO = context.GetFramebuffer("Lighting");
        if (!gbufferFBO || !lightingFBO) return;

        cmd.BeginRenderPass(lightingFBO, true, glm::vec4(0.0f));

        // PBR Deferred Shading
        cmd.BindPipeline(m_DeferredPipeline);
        cmd.BindTexture2D(m_DeferredPipeline, "g_Position", 0, gbufferFBO, 0);
        cmd.BindTexture2D(m_DeferredPipeline, "g_Normal", 1, gbufferFBO, 1);
        cmd.BindTexture2D(m_DeferredPipeline, "g_Albedo", 2, gbufferFBO, 2);
        cmd.BindTexture2D(m_DeferredPipeline, "g_PBR", 3, gbufferFBO, 3);
        cmd.BindTexture2D(m_DeferredPipeline, "g_CustomData", 4, gbufferFBO, 4);

        auto irrMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap", nullptr);
        auto preMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap", nullptr);
        if (irrMap && preMap) {
            cmd.BindTextureCube(m_DeferredPipeline, "u_IrradianceMap", 8, irrMap);
            cmd.BindTextureCube(m_DeferredPipeline, "u_PrefilteredMap", 9, preMap);
        }
        auto brdf = context.GetTexture("BRDFLUT");
        auto whiteTex = context.GetTexture("WhiteTexture");
        if (brdf) cmd.BindTexture2D(m_DeferredPipeline, "u_BRDFLUT", 10, brdf);
        else if (whiteTex) cmd.BindTexture2D(m_DeferredPipeline, "u_BRDFLUT", 10, whiteTex);

        auto shadowFBO = context.GetFramebuffer("ShadowMap");
        if (shadowFBO)
            cmd.BindTexture2D(m_DeferredPipeline, "u_ShadowMap", 5, shadowFBO, 0, true);
        else if (whiteTex)
            cmd.BindTexture2D(m_DeferredPipeline, "u_ShadowMap", 5, whiteTex);

        DeferredLightingPushConstants defPC{};
        defPC.LightSpaceMatrix = context.Get<glm::mat4>("LightSpaceMatrix", glm::mat4(1.0f));
        defPC.AmbientColor = context.Get<glm::vec3>("EnvironmentAmbientColor", glm::vec3(0.1f));
        defPC.Intensity = context.Get<float>("EnvironmentIntensity", 1.0f);
        defPC.EnvMapEnabled = (irrMap && preMap) ? 1 : 0;
        cmd.PushConstantData(m_DeferredPipeline, &defPC, sizeof(DeferredLightingPushConstants));
        cmd.DrawArrays(3);

        // Skybox (depth already written by deferred shader via gl_FragDepth)
        auto envMap = context.Get<std::shared_ptr<TextureCube>>("EnvironmentCubemap", nullptr);
        if (envMap && context.Get<bool>("ShowSkybox", true)) {
            cmd.BindPipeline(m_SkyboxPipeline);
            cmd.BindTextureCube(m_SkyboxPipeline, "u_Skybox", 0, envMap);
            struct alignas(16) { glm::mat4 VP; float I; } pc;
            glm::mat4 v = glm::mat4(glm::mat3(context.ViewMatrix));
            pc.VP = context.ProjectionMatrix * v;
            pc.I = context.Get<float>("EnvironmentIntensity", 1.0f);
            cmd.PushConstantData(m_SkyboxPipeline, &pc, sizeof pc);
            auto mesh = context.Get<std::shared_ptr<Mesh>>("SkyboxMesh", nullptr);
            if (mesh) cmd.DrawIndexed(mesh, mesh->GetIndexCount());
        }

        cmd.EndRenderPass();
        context.Framebuffers["Lighting"] = lightingFBO;
    }
}
