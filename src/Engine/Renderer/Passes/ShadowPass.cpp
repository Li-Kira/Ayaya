#include "ayapch.h"
#include "ShadowPass.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Material.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    ShadowPass::ShadowPass() { m_PassName = "Shadow Map Pass"; }

    void ShadowPass::OnAttach() {
        m_ShadowShader = Shader::Create("Shadow/shadow_map.vert", "Shadow/shadow_map.frag");
        
        // 1. 使用统一的 FBO 抽象
        FramebufferSpecification spec;
        spec.Width = 2048; 
        spec.Height = 2048;
        spec.Samples = 1;
        spec.Attachments = { FramebufferTextureFormat::Depth }; 
        m_ShadowMapFBO = Framebuffer::Create(spec);

        // 2. 组装阴影专属 PSO
        PipelineSpecification pipeSpec;
        pipeSpec.Shader = m_ShadowShader;
        pipeSpec.TargetFramebuffer = m_ShadowMapFBO;
        pipeSpec.DepthTest = true;
        pipeSpec.DepthWrite = true;
        
        // 改为 CullMode::Back（常规模型）或者 CullMode::None（如果你的场景里有单面的纸片/树叶也要产生阴影）
        pipeSpec.BackfaceCulling = CullMode::Back; 
        
        m_Pipeline = Pipeline::Create(pipeSpec);
    }

    void ShadowPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto lightView = context.ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();

        // Scan for active directional lights (respecting visibility toggle)
        glm::vec3 lightDir = glm::vec3(0.0f);
        bool hasActiveLight = false;
        for (auto entityID : lightView) {
            Entity entity{entityID, context.ActiveScene.get()};
            if (!entity.IsActiveInHierarchy()) continue;
            auto& tc = lightView.get<TransformComponent>(entityID);
            lightDir = glm::normalize(glm::vec3(tc.GetTransform() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            hasActiveLight = true;
            break;
        }

        if (!hasActiveLight) {
            context.Set("ShadowMap_Output", std::shared_ptr<Framebuffer>(nullptr));
            return;
        }

        glm::vec3 lightPos = -lightDir * 20.0f;
        glm::mat4 lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 50.0f);
        glm::mat4 lightViewMatrix = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightSpaceMatrix = lightProjection * lightViewMatrix;

        cmd.BeginRenderPass(m_ShadowMapFBO, true, glm::vec4(1.0f));
        cmd.BindPipeline(m_Pipeline); 
        context.Stats.ShaderBinds++;

        cmd.PushConstant(m_Pipeline, "u_LightSpaceMatrix", lightSpaceMatrix);

        auto meshView = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshView) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;
            
            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            if (!meshComp.CastShadows) continue;
            auto material = AssetManager::GetAsset<Material>(meshComp.MaterialHandle);
            if (material && material->GetBlendMode() == MaterialBlendMode::Translucent) continue;
            auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
            if (!model) continue; 

            cmd.PushConstant(m_Pipeline, "u_Transform", entity.GetWorldTransform());
            
            for (auto& mesh : model->GetMeshes()) {
                std::string tag = entity.GetComponent<TagComponent>().Tag;
                uint32_t tris = mesh->GetIndexCount() / 3;

                if (context.RecordAndCheckDrawCall("Shadow Pass", tag, "Shadow Map", tris)) {
                    cmd.DrawIndexed(mesh, mesh->GetIndexCount());
                }
            }
        }
        
        cmd.EndRenderPass();

        // ==========================================
        // 【核心交接】：将阴影 FBO 实体挂上全局黑板！
        // 后续的 LightingPass 会读取这个对象进行深度图采样
        // ==========================================
        context.Set("ShadowMap_Output", m_ShadowMapFBO);
        context.Framebuffers["ShadowMap"] = m_ShadowMapFBO;
        context.Set("LightSpaceMatrix", lightSpaceMatrix);
    }
}