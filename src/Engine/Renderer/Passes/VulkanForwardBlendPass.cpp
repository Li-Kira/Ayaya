#include "ayapch.h"
#include "VulkanForwardBlendPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/TextureCube.hpp"
#include "Asset/AssetManager.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"

namespace Ayaya {

    void VulkanForwardBlendPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        FramebufferSpecification hdr;
        hdr.Width = width; hdr.Height = height; hdr.Samples = 1;
        hdr.Attachments = {FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth};
        builder.WriteTexture("Lighting", hdr);
    }

    void VulkanForwardBlendPass::OnAttach() {
        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = {FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth};
        m_RefFBO = Framebuffer::Create(ref);

        m_SkyboxShader = Shader::Create("Skybox/skybox.vert", "Skybox/skybox.frag");
        m_SkyboxPipeSpec.Shader = m_SkyboxShader;
        m_SkyboxPipeSpec.Layout = {{ShaderDataType::Float3,"a_Position"},{ShaderDataType::Float3,"a_Normal"},{ShaderDataType::Float2,"a_TexCoord"},{ShaderDataType::Float3,"a_Tangent"}};
        m_SkyboxPipeSpec.TargetFramebuffer = m_RefFBO;
        m_SkyboxPipeSpec.DepthTest = true; m_SkyboxPipeSpec.DepthWrite = false;
        m_SkyboxPipeSpec.DepthOperator = DepthCompareOperator::LEqual;
        m_SkyboxPipeSpec.BackfaceCulling = CullMode::None;
        m_SkyboxPipeline = Pipeline::Create(m_SkyboxPipeSpec);

        m_GridShader = Shader::Create("UI/grid.vert", "UI/grid.frag");
        m_GridMesh = Mesh::CreatePlane(2000.f, 2000.f);
        m_GridPipeSpec.Shader = m_GridShader;
        m_GridPipeSpec.Layout = {{ShaderDataType::Float3,"a_Position"},{ShaderDataType::Float3,"a_Normal"},{ShaderDataType::Float2,"a_TexCoord"},{ShaderDataType::Float3,"a_Tangent"}};
        m_GridPipeSpec.TargetFramebuffer = m_RefFBO;
        m_GridPipeSpec.DepthTest = true; m_GridPipeSpec.DepthWrite = false;
        m_GridPipeSpec.DepthOperator = DepthCompareOperator::Less;
        m_GridPipeSpec.Blend = true; m_GridPipeSpec.BlendMode = BlendModeType::Alpha;
        m_GridPipeSpec.BackfaceCulling = CullMode::None;
        m_GridPipeline = Pipeline::Create(m_GridPipeSpec);

        m_SpriteShader = Shader::Create("2D/sprite.vert", "2D/sprite.frag");
        m_SpritePipeSpec.Shader = m_SpriteShader;
        m_SpritePipeSpec.Layout = {};
        m_SpritePipeSpec.TargetFramebuffer = m_RefFBO;
        m_SpritePipeSpec.DepthTest = true;
        m_SpritePipeSpec.DepthWrite = false;
        m_SpritePipeSpec.Blend = true; m_SpritePipeSpec.BlendMode = BlendModeType::Alpha;
        m_SpritePipeSpec.BackfaceCulling = CullMode::None;
        m_SpritePipeline = Pipeline::Create(m_SpritePipeSpec);
    }

    void VulkanForwardBlendPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto fbo = context.GetFramebuffer("Lighting");
        if (!fbo) return;

        cmd.BeginRenderPass(fbo, false);
        RenderSkybox(context, cmd);
        RenderGrid(context, cmd);
        RenderSprites(context, cmd);
        cmd.EndRenderPass();
        context.Framebuffers["Lighting"] = fbo;
    }

    void VulkanForwardBlendPass::RenderSkybox(RenderContext& ctx, RenderCommandBuffer& cmd) {
        auto envMap = ctx.Get<std::shared_ptr<TextureCube>>("EnvironmentCubemap", nullptr);
        if (!envMap || !ctx.Get<bool>("ShowSkybox", true)) return;
        cmd.BindPipeline(m_SkyboxPipeline);
        cmd.BindTextureCube(m_SkyboxPipeline, "u_Skybox", 0, envMap);
        struct alignas(16) { glm::mat4 VP; float I; } pc;
        // Skybox requires perspective projection; fall back when orthographic.
        // ctx.ProjectionMatrix includes the Vulkan Y-flip correction —
        // our replacement perspective matrix must include the same flip.
        glm::mat4 skyProj = ctx.ProjectionMatrix;
        if (skyProj[3][3] == 1.0f) {
            auto fbo = ctx.GetFramebuffer("Lighting");
            float aspect = fbo ? (float)fbo->GetSpecification().Width
                                 / (float)fbo->GetSpecification().Height : 1.778f;
            skyProj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
            skyProj[1][1] *= -1.0f; // Vulkan Y-flip
        }
        pc.VP = skyProj * glm::mat4(glm::mat3(ctx.ViewMatrix));
        pc.I = ctx.Get<float>("EnvironmentIntensity", 1.0f);
        cmd.PushConstantData(m_SkyboxPipeline, &pc, sizeof pc);
        auto mesh = ctx.Get<std::shared_ptr<Mesh>>("SkyboxMesh", nullptr);
        if (mesh) cmd.DrawIndexed(mesh, mesh->GetIndexCount());
    }

    void VulkanForwardBlendPass::RenderGrid(RenderContext& ctx, RenderCommandBuffer& cmd) {
        if (!ctx.Get<bool>("ShowGrid", false)) return;
        if (!m_GridMesh) return;
        cmd.BindPipeline(m_GridPipeline);
        struct alignas(16) { glm::mat4 t; float e; } pc;
        pc.t = glm::mat4(1.0f);
        pc.e = 1.f / ctx.Get<float>("PhysicalExposure", 1.f);
        cmd.PushConstantData(m_GridPipeline, &pc, sizeof pc);
        cmd.DrawIndexed(m_GridMesh, m_GridMesh->GetIndexCount());
    }

    void VulkanForwardBlendPass::RenderSprites(RenderContext& ctx, RenderCommandBuffer& cmd) {
        struct SpriteDrawCmd { glm::mat4 Transform; SpriteRendererComponent Comp; float Distance; };
        std::vector<SpriteDrawCmd> list;
        auto group = ctx.ActiveScene->Reg().view<TransformComponent, SpriteRendererComponent>();
        for (auto id : group) {
            Entity e{ id, ctx.ActiveScene.get() };
            if (!e.IsActiveInHierarchy()) continue;
            auto [t, sc] = group.get<TransformComponent, SpriteRendererComponent>(id);
            glm::mat4 xform = e.GetWorldTransform();
            float dist = glm::length(ctx.CameraPosition - glm::vec3(xform[3]));
            list.push_back({xform, sc, dist});
        }
        if (list.empty()) return;

        // Painter's Algorithm: sort far→near for correct alpha blending
        std::sort(list.begin(), list.end(),
            [](auto& a, auto& b) { return a.Distance > b.Distance; });

        cmd.BindPipeline(m_SpritePipeline);
        auto whiteTex = ctx.GetTexture("WhiteTexture");
        for (auto& s : list) {
            struct { glm::mat4 Transform; glm::vec4 Color; float ExposureInv; int UseTexture; } pc;
            pc.Transform = s.Transform;
            pc.Color = s.Comp.Color;
            pc.ExposureInv = 1.f / ctx.Get<float>("PhysicalExposure", 1.f);

            if (s.Comp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(s.Comp.TextureHandle)) {
                auto tex = AssetManager::GetAsset<Texture2D>(s.Comp.TextureHandle);
                if (tex) { cmd.BindTexture2D(m_SpritePipeline, "u_Texture", 0, tex); pc.UseTexture = 1; }
                else     { cmd.BindTexture2D(m_SpritePipeline, "u_Texture", 0, whiteTex); pc.UseTexture = 0; }
            } else {
                cmd.BindTexture2D(m_SpritePipeline, "u_Texture", 0, whiteTex);
                pc.UseTexture = 0;
            }
            cmd.PushConstantData(m_SpritePipeline, &pc, sizeof pc);
            cmd.DrawTriangleStrip(4);
        }
    }

}
