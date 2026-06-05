#include "ayapch.h"
#include "VulkanGbufferPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Frustum.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"

namespace Ayaya {

    void VulkanGBufferPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        FramebufferSpecification spec;
        spec.Width = width; spec.Height = height; spec.Samples = 1;
        spec.Attachments = {
            FramebufferTextureFormat::RGBA32F, FramebufferTextureFormat::RGBA16F,
            FramebufferTextureFormat::RGBA8,   FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::RGBA8,   FramebufferTextureFormat::Depth
        };
        builder.WriteTexture("GBuffer", spec);
    }

    void VulkanGBufferPass::OnAttach() {
        m_GBufferShader = Shader::Create("Deferred/gbuffer.vert", "Deferred/gbuffer.frag");
        m_PipeSpec.Shader = m_GBufferShader;
        m_PipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        m_PipeSpec.DepthTest = true;
        m_PipeSpec.DepthWrite = true;
        m_PipeSpec.Blend = false;
        m_PipeSpec.BackfaceCulling = CullMode::None;

        // Pre-create pipeline with format reference FBO (same format as RenderGraph)
        FramebufferSpecification refSpec;
        refSpec.Width = 1280; refSpec.Height = 720; refSpec.Samples = 1;
        refSpec.Attachments = {
            FramebufferTextureFormat::RGBA32F, FramebufferTextureFormat::RGBA16F,
            FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth
        };
        m_RefFBO = Framebuffer::Create(refSpec);
        m_PipeSpec.TargetFramebuffer = m_RefFBO;
        m_Pipeline = Pipeline::Create(m_PipeSpec);
    }

    void VulkanGBufferPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto fbo = context.GetFramebuffer("GBuffer");
        if (!fbo) return;

        glm::mat4 viewProj = context.ProjectionMatrix * context.ViewMatrix;
        Frustum frustum(viewProj);

        m_DrawList.clear();
        auto view = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : view) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;
            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
            if (!model) continue;
            glm::mat4 transform = entity.GetWorldTransform();
            VulkanGBufferCommandData d;
            d.Transform = transform;
            d.TargetEntity = entity;
            d.ReceiveShadows = meshComp.ReceiveShadows;
            d.MaterialAsset = AssetManager::GetAsset<Material>(meshComp.MaterialHandle);
            for (auto& mesh : model->GetMeshes()) {
                if (!frustum.IsBoxVisible(mesh->GetAABB(), transform)) continue;
                d.MeshAsset = mesh;
                m_DrawList.push_back(d);
            }
        }

        cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
        cmd.BindPipeline(m_Pipeline);
        auto whiteTex = context.GetTexture("WhiteTexture");

        for (auto& d : m_DrawList) {
            // Bind fallback white textures for ALL 5 slots (GBuffer shader bindings 1-5)
            if (whiteTex) {
                cmd.BindTexture2D(m_Pipeline, "u_AlbedoMap",    1, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_MetallicMap",  2, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_RoughnessMap", 3, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_AOMap",        4, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_NormalMap",    5, whiteTex);
            }

            GBufferPushConstants pc{};
            pc.Transform = d.Transform;
            pc.ReceiveShadows = d.ReceiveShadows ? 1.0f : 0.0f;
            pc.Metallic = 0.0f;
            pc.Roughness = 0.5f;
            pc.AO = 1.0f;

            Entity selected = context.Get<Entity>("SelectedEntity", Entity{});
            Entity hovered  = context.Get<Entity>("HoveredEntity", Entity{});
            pc.IsSelected = (d.TargetEntity == selected || d.TargetEntity == hovered) ? 1 : 0;

            if (d.MaterialAsset) {
                for (auto& prop : d.MaterialAsset->Properties) {
                    if (prop.Type == MaterialPropertyType::Vec3 && prop.UniformName == "u_Albedo")
                        pc.Albedo = prop.Vec3Value;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Metallic")
                        pc.Metallic = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Roughness")
                        pc.Roughness = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_AO")
                        pc.AO = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Texture2D) {
                        bool hasTex = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) || (prop.RuntimeTexture != nullptr);
                        if (hasTex) {
                            auto tex = prop.RuntimeTexture ? prop.RuntimeTexture : AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                            if (prop.UniformName == "u_AlbedoMap")    { cmd.BindTexture2D(m_Pipeline, "u_AlbedoMap", 1, tex); pc.UseAlbedoMap = 1; }
                            else if (prop.UniformName == "u_MetallicMap")  { cmd.BindTexture2D(m_Pipeline, "u_MetallicMap", 2, tex); pc.UseMetallicMap = 1; }
                            else if (prop.UniformName == "u_RoughnessMap") { cmd.BindTexture2D(m_Pipeline, "u_RoughnessMap", 3, tex); pc.UseRoughnessMap = 1; }
                            else if (prop.UniformName == "u_AOMap")        { cmd.BindTexture2D(m_Pipeline, "u_AOMap", 4, tex); pc.UseAOMap = 1; }
                            else if (prop.UniformName == "u_NormalMap")    { cmd.BindTexture2D(m_Pipeline, "u_NormalMap", 5, tex); pc.UseNormalMap = 1; }
                        }
                    }
                }
            }
            cmd.PushConstantData(m_Pipeline, &pc, sizeof(GBufferPushConstants));
            cmd.DrawIndexed(d.MeshAsset, d.MeshAsset->GetIndexCount());
        }

        cmd.EndRenderPass();
        // RenderGraph InsertTileResolveBarrier handles all attachments
        context.Framebuffers["GBuffer"] = fbo;
    }
}
