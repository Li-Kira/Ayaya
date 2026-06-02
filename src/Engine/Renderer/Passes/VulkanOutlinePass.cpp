#include "ayapch.h"
#include "VulkanOutlinePass.hpp"
#include "Asset/AssetManager.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"

namespace Ayaya {

    VulkanOutlinePass::VulkanOutlinePass() {
        m_PassName = "Vulkan Outline Pass";
    }

    void VulkanOutlinePass::OnAttach() {
        m_OutlineShader = Shader::Create("UI/outline.vert", "UI/outline.frag");
        m_OutlinePipeSpec.Shader = m_OutlineShader;
        m_OutlinePipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal"   },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent"  },
        };
        m_OutlinePipeSpec.DepthTest = true;
        m_OutlinePipeSpec.DepthWrite = false;
        m_OutlinePipeSpec.Blend = false;
        m_OutlinePipeSpec.BackfaceCulling = CullMode::None;
    }

    void VulkanOutlinePass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        FramebufferSpecification selSpec;
        selSpec.Width  = width;
        selSpec.Height = height;
        selSpec.Samples = 1;
        selSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        builder.WriteTexture("Selection", selSpec);
    }

    void VulkanOutlinePass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width  = context.Get<uint32_t>("ViewportWidth", 1280);
        uint32_t height = context.Get<uint32_t>("ViewportHeight", 720);
        if (width == 0 || height == 0) return;

        auto selectionFBO = context.GetFramebuffer("Selection");
        if (!selectionFBO) return;

        if (!m_OutlinePipeline) {
            m_OutlinePipeSpec.TargetFramebuffer = selectionFBO;
            m_OutlinePipeline = Pipeline::Create(m_OutlinePipeSpec);
        }

        cmd.BeginRenderPass(selectionFBO, true, glm::vec4(0.0f));

        Entity hoveredEntity = context.Get<Entity>("HoveredEntity", Entity{});
        if (hoveredEntity && hoveredEntity.IsActiveInHierarchy()) {

            if (hoveredEntity.HasComponent<MeshRendererComponent>()) {
                auto& meshComp = hoveredEntity.GetComponent<MeshRendererComponent>();
                auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
                if (model) {
                    cmd.BindPipeline(m_OutlinePipeline);
                    context.Stats.ShaderBinds++;
                    glm::mat4 transform = hoveredEntity.GetWorldTransform();
                    struct { glm::mat4 Transform; alignas(16) glm::vec3 Color; } outlinePC;
                    outlinePC.Transform = transform;
                    outlinePC.Color = glm::vec3(1.0f, 0.5f, 0.0f);
                    for (auto& mesh : model->GetMeshes()) {
                        cmd.PushConstantData(m_OutlinePipeline, &outlinePC, sizeof(outlinePC));
                        cmd.DrawIndexed(mesh, mesh->GetIndexCount());
                    }
                }
            }
        }

        cmd.EndRenderPass();
        context.Framebuffers["Selection"] = selectionFBO;
    }

}
