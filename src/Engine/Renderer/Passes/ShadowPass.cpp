#include "ayapch.h"
#include "ShadowPass.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    ShadowPass::ShadowPass() { m_PassName = "Shadow Map Pass"; }

    void ShadowPass::OnAttach() {
        m_ShadowShader = Shader::Create("assets/Editor/shaders/Shadow/shadow_map.vert", "assets/Editor/shaders/Shadow/shadow_map.frag");
        
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
        
        // 【核心修复】：千万不要用 Front！
        // 改为 CullMode::Back（常规模型）或者 CullMode::None（如果你的场景里有单面的纸片/树叶也要产生阴影）
        pipeSpec.BackfaceCulling = CullMode::Back; 
        
        m_Pipeline = Pipeline::Create(pipeSpec);
    }

    void ShadowPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto lightView = context.ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();
        if (lightView.begin() == lightView.end()) {
            context.Set("ShadowMap_Output", (uint32_t)0);
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

        m_ShadowMapFBO->Bind();
        cmd.SetViewport(0, 0, 2048, 2048);
        
        // 【核心进化】：先绑定管线，再清空！管线会自动配置好深度写入状态！
        cmd.BindPipeline(m_Pipeline); 
        cmd.Clear(); 
        
        context.Stats.ShaderBinds++;
        m_ShadowShader->SetMat4("u_LightSpaceMatrix", lightSpaceMatrix);

        auto meshView = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshView) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;
            
            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            if (!meshComp.ModelAsset || !meshComp.CastShadows) continue; 

            m_ShadowShader->SetMat4("u_Transform", entity.GetWorldTransform());
            
            for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                std::string tag = entity.GetComponent<TagComponent>().Tag;
                uint32_t tris = mesh->GetIndexCount() / 3;

                if (context.RecordAndCheckDrawCall("Shadow Pass", tag, "Shadow Map", tris)) {
                    cmd.DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
                }
            }
        }
        
        m_ShadowMapFBO->Unbind();
        
        uint32_t vpWidth = context.Get<uint32_t>("ViewportWidth", 1280);
        uint32_t vpHeight = context.Get<uint32_t>("ViewportHeight", 720);
        cmd.SetViewport(0, 0, vpWidth, vpHeight);

        context.Set("ShadowMap_Output", m_ShadowMapFBO->GetDepthAttachmentRendererID());
        context.Set("LightSpaceMatrix", lightSpaceMatrix);
    }
}