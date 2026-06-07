#include "ayapch.h"
#include "VulkanOutlinePass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/VertexArray.hpp"
#include "Asset/AssetManager.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"

namespace Ayaya {

    void VulkanOutlinePass::DeclareResources(RGBuilder& builder, uint32_t w, uint32_t h) {
        builder.ReadTexture("GBuffer");
        FramebufferSpecification sel;
        sel.Width = w; sel.Height = h; sel.Samples = 1;
        sel.Attachments = {FramebufferTextureFormat::RGBA8};
        builder.WriteTexture("Selection", sel);
    }

    void VulkanOutlinePass::OnAttach() {
        // Full-screen mask extraction from GBuffer (visible parts)
        m_EmptyVAO = VertexArray::Create();
        m_MaskShader = Shader::Create("PostProcess/postprocess.vert", "UI/selection_mask.frag");
        m_MaskPipeSpec.Shader = m_MaskShader;
        m_MaskPipeSpec.Layout = {};
        m_MaskPipeSpec.DepthTest = false;
        m_MaskPipeSpec.DepthWrite = false;
        m_MaskPipeSpec.Blend = false;
        m_MaskPipeSpec.BackfaceCulling = CullMode::None;

        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = {FramebufferTextureFormat::RGBA8};
        m_RefFBO = Framebuffer::Create(ref);

        // Mesh silhouette pipeline
        m_GeomShader = Shader::Create("UI/outline.vert", "UI/outline.frag");
        m_GeomPipeSpec.Shader = m_GeomShader;
        m_GeomPipeSpec.Layout = {{ShaderDataType::Float3,"a_Position"},{ShaderDataType::Float3,"a_Normal"},{ShaderDataType::Float2,"a_TexCoord"},{ShaderDataType::Float3,"a_Tangent"}};
        m_GeomPipeSpec.TargetFramebuffer = m_RefFBO;
        m_GeomPipeSpec.NoTextureDescriptors = true;
        m_GeomPipeSpec.DepthTest = false;
        m_GeomPipeSpec.DepthWrite = false;
        m_GeomPipeSpec.Blend = false;
        m_GeomPipeSpec.BackfaceCulling = CullMode::None;

        // Sprite silhouette pipeline (vertex-index-driven quad, pure white fill)
        m_SpriteShader = Shader::Create("2D/sprite.vert", "2D/sprite.frag");
        m_SpritePipeSpec.Shader = m_SpriteShader;
        m_SpritePipeSpec.Layout = {}; // gl_VertexIndex driven, no vertex attributes
        m_SpritePipeSpec.TargetFramebuffer = m_RefFBO;
        m_SpritePipeSpec.DepthTest = false;
        m_SpritePipeSpec.DepthWrite = false;
        m_SpritePipeSpec.Blend = false;
        m_SpritePipeSpec.BackfaceCulling = CullMode::None;
        m_SpritePipeSpec.Topology = PrimitiveTopology::TriangleStrip;
    }

    void VulkanOutlinePass::Execute(RenderContext& ctx, RenderCommandBuffer& cmd) {
        auto gbufferFBO = ctx.GetFramebuffer("GBuffer");
        auto selFBO = ctx.GetFramebuffer("Selection");
        if (!selFBO) return;

        // Lazy pipeline init
        if (!m_MaskPipeline) {
            m_MaskPipeSpec.TargetFramebuffer = selFBO;
            m_MaskPipeline = Pipeline::Create(m_MaskPipeSpec);
            m_GeomPipeSpec.TargetFramebuffer = selFBO;
            m_GeomPipeline = Pipeline::Create(m_GeomPipeSpec);
            m_SpritePipeSpec.TargetFramebuffer = selFBO;
            m_SpritePipeline = Pipeline::Create(m_SpritePipeSpec);
        }

        // Step 1: Extract visible mask from GBuffer (only pixels where selected entity is visible)
        cmd.BeginRenderPass(selFBO, true, glm::vec4(0.0f));
        if (gbufferFBO) {
            cmd.BindPipeline(m_MaskPipeline);
            cmd.BindTexture2D(m_MaskPipeline, "u_CustomData", 0, gbufferFBO, 4);
            cmd.DrawArrays(m_EmptyVAO, 3);
        }

        // Step 2: Render selected/hovered entity geometry → fills occluded silhouette.
        // Walk entity hierarchy: if the selected entity is a container node (no mesh),
        // recursively render child meshes instead.
        Entity sel = ctx.Get<Entity>("SelectedEntity", Entity{});
        Entity hov = ctx.Get<Entity>("HoveredEntity", Entity{});

        Scene* scene = sel ? sel.GetScene() : (hov ? hov.GetScene() : nullptr);

        std::function<void(Entity, const glm::mat4&)> renderRecursive;
        renderRecursive = [&](Entity e, const glm::mat4& worldTransform) {
            if (!e) return;

            // Render this entity's mesh silhouette
            if (e.HasComponent<MeshRendererComponent>()) {
                auto& mc = e.GetComponent<MeshRendererComponent>();
                auto model = AssetManager::GetAsset<Model>(mc.ModelHandle);
                if (model) {
                    cmd.BindPipeline(m_GeomPipeline);
                    struct alignas(16) { glm::mat4 Transform; alignas(16) glm::vec3 Color; } pc;
                    pc.Color = glm::vec3(1.0f);
                    pc.Transform = worldTransform;
                    for (auto& mesh : model->GetMeshes()) {
                        cmd.PushConstantData(m_GeomPipeline, &pc, sizeof pc);
                        cmd.DrawIndexed(mesh, mesh->GetIndexCount());
                    }
                }
            }
            else if (e.HasComponent<SpriteRendererComponent>()) {
                cmd.BindPipeline(m_SpritePipeline);
                struct alignas(16) {
                    glm::mat4 Transform; glm::vec4 Color;
                    float ExposureInverse; int UseTexture;
                } pc;
                pc.Transform = worldTransform;
                pc.Color = glm::vec4(1.0f);
                pc.ExposureInverse = 1.0f;
                pc.UseTexture = 0;
                cmd.PushConstantData(m_SpritePipeline, &pc, sizeof pc);
                cmd.DrawArrays(4);
            }

            // Recurse into children
            if (scene && e.HasComponent<RelationshipComponent>()) {
                auto& rel = e.GetComponent<RelationshipComponent>();
                for (auto childHandle : rel.Children) {
                    Entity child{childHandle, scene};
                    if (!child || !child.HasComponent<TransformComponent>()) continue;
                    glm::mat4 childWorld = worldTransform *
                        child.GetComponent<TransformComponent>().GetTransform();
                    renderRecursive(child, childWorld);
                }
            }
        };

        // Use GetWorldTransform() as starting point — includes all ancestor transforms
        if (sel) renderRecursive(sel, sel.GetWorldTransform());
        if (hov && hov != sel) renderRecursive(hov, hov.GetWorldTransform());

        cmd.EndRenderPass();
        ctx.Framebuffers["Selection"] = selFBO;
    }
}
