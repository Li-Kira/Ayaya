#include "ayapch.h"
#include "VulkanShadowPass.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Material.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    VulkanShadowPass::VulkanShadowPass() { m_PassName = "Shadow Map Pass"; }

    void VulkanShadowPass::OnAttach() {
        m_ShadowShader = Shader::Create("Shadow/shadow_map.vert", "Shadow/shadow_map.frag");

        m_PipeSpec.Shader = m_ShadowShader;
        m_PipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal"   },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent"  },
        };
        m_PipeSpec.DepthTest = true;
        m_PipeSpec.DepthWrite = true;
        m_PipeSpec.BackfaceCulling = CullMode::Back;
    }

    void VulkanShadowPass::DeclareResources(RGBuilder& builder) {
        FramebufferSpecification spec;
        spec.Width  = 2048;
        spec.Height = 2048;
        spec.Samples = 1;
        spec.Attachments = { FramebufferTextureFormat::Depth };
        builder.WriteTexture("ShadowMap", spec);
    }

    void VulkanShadowPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto shadowFBO = context.GetFramebuffer("ShadowMap");
        if (!shadowFBO) return;

        if (!m_Pipeline) {
            m_PipeSpec.TargetFramebuffer = shadowFBO;
            m_Pipeline = Pipeline::Create(m_PipeSpec);
        }

        auto lightView = context.ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();
        bool hasLight = lightView.begin() != lightView.end();

        glm::mat4 lightSpaceMatrix(1.0f);

        if (hasLight) {
            glm::vec3 lightDir(0.0f);
            for (auto entityID : lightView) {
                auto& tc = lightView.get<TransformComponent>(entityID);
                lightDir = glm::normalize(glm::vec3(tc.GetTransform() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                break;
            }
            glm::vec3 lightPos = -lightDir * 20.0f;
            glm::mat4 lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 50.0f);
            // GLM ortho produces [-1,1] depth; Vulkan clips to [0,w]; correct to [0,1]
            glm::mat4 depthCorrection = glm::mat4(1.0f);
            depthCorrection[2][2] = 0.5f;
            depthCorrection[3][2] = 0.5f;
            lightProjection = depthCorrection * lightProjection;
            glm::mat4 lightViewMatrix = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            lightSpaceMatrix = lightProjection * lightViewMatrix;
        }

        static_assert(sizeof(VulkanShadowPushConstants) == 128, "Shadow push constant size mismatch");

        // 始终执行 clear，避免 ForwardPass 读到未初始化的 ShadowMap
        cmd.BeginRenderPass(shadowFBO, true, glm::vec4(1.0f));

        if (hasLight) {
            cmd.BindPipeline(m_Pipeline);
            context.Stats.ShaderBinds++;

            auto meshView = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
            for (auto entityID : meshView) {
                Entity entity{ entityID, context.ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue;
                auto& meshComp = entity.GetComponent<MeshRendererComponent>();
                if (!meshComp.CastShadows) continue;
                // Translucent (WBOIT) objects never cast shadows
                auto material = AssetManager::GetAsset<Material>(meshComp.MaterialHandle);
                if (material && material->GetBlendMode() == MaterialBlendMode::Translucent) continue;
                auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
                if (!model) continue;

                VulkanShadowPushConstants constants{};
                constants.LightSpaceMatrix = lightSpaceMatrix;
                constants.Transform = entity.GetWorldTransform();
                cmd.PushConstantData(m_Pipeline, &constants, sizeof(VulkanShadowPushConstants));

                for (auto& mesh : model->GetMeshes()) {
                    std::string tag = entity.GetComponent<TagComponent>().Tag;
                    uint32_t tris = mesh->GetIndexCount() / 3;
                    if (context.RecordAndCheckDrawCall("Shadow Pass", tag, "Shadow Map", tris))
                        cmd.DrawIndexed(mesh, mesh->GetIndexCount());
                }
            }

        }

        cmd.EndRenderPass();

        context.Set("LightSpaceMatrix", lightSpaceMatrix);
        context.Set("ShadowMap_Output", shadowFBO);
    }

}
