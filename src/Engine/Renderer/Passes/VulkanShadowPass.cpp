#include "ayapch.h"
#include "VulkanShadowPass.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Asset/AssetManager.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    VulkanShadowPass::VulkanShadowPass() { m_PassName = "Shadow Map Pass"; }

    void VulkanShadowPass::OnAttach() {
        m_ShadowShader = Shader::Create("Shadow/shadow_map.vert", "Shadow/shadow_map.frag");

        FramebufferSpecification spec;
        spec.Width = 2048;
        spec.Height = 2048;
        spec.Samples = 1;
        spec.Attachments = { FramebufferTextureFormat::Depth };
        m_ShadowMapFBO = Framebuffer::Create(spec);

        PipelineSpecification pipeSpec;
        pipeSpec.Shader = m_ShadowShader;
        pipeSpec.TargetFramebuffer = m_ShadowMapFBO;
        pipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" }
        };
        pipeSpec.DepthTest = true;
        pipeSpec.DepthWrite = true;
        pipeSpec.BackfaceCulling = CullMode::Back;

        m_Pipeline = Pipeline::Create(pipeSpec);
    }

    void VulkanShadowPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto lightView = context.ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();
        if (lightView.begin() == lightView.end()) {
            context.Set("ShadowMap_Output", std::shared_ptr<Framebuffer>(nullptr));
            return;
        }

        glm::vec3 lightDir = glm::vec3(0.0f);
        for (auto entityID : lightView) {
            auto& tc = lightView.get<TransformComponent>(entityID);
            lightDir = glm::normalize(glm::vec3(tc.GetTransform() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            break;
        }

        glm::vec3 lightPos = -lightDir * 20.0f;
        glm::mat4 lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 50.0f);
        glm::mat4 lightViewMatrix = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightSpaceMatrix = lightProjection * lightViewMatrix;

        cmd.BeginRenderPass(m_ShadowMapFBO, true, glm::vec4(1.0f));
        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;

        auto meshView = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshView) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;

            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
            if (!model || !meshComp.CastShadows) continue;

            VulkanShadowPushConstants constants{};
            constants.LightSpaceMatrix = lightSpaceMatrix;
            constants.Transform = entity.GetWorldTransform();
            cmd.PushConstantData(m_Pipeline, &constants, sizeof(VulkanShadowPushConstants));

            for (auto& mesh : model->GetMeshes()) {
                std::string tag = entity.GetComponent<TagComponent>().Tag;
                uint32_t tris = mesh->GetIndexCount() / 3;

                if (context.RecordAndCheckDrawCall("Shadow Pass", tag, "Shadow Map", tris)) {
                    cmd.DrawIndexed(mesh, mesh->GetIndexCount());
                }
            }
        }

        cmd.EndRenderPass();
        cmd.InsertExecutionBarrier(); // flush tile writes before LightingPass reads ShadowMap

        context.Set("ShadowMap_Output", m_ShadowMapFBO);
        context.Framebuffers["ShadowMap"] = m_ShadowMapFBO;
        context.Set("LightSpaceMatrix", lightSpaceMatrix);
    }

}
