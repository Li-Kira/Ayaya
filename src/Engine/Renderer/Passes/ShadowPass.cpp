#include "ayapch.h"
#include "ShadowPass.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    ShadowPass::ShadowPass() {
        m_PassName = "Shadow Map Pass";
    }

    void ShadowPass::OnAttach() {
        m_ShadowShader = Shader::Create("assets/Editor/shaders/Shadow/shadow_map.vert", "assets/Editor/shaders/Shadow/shadow_map.frag");
        
        FramebufferSpecification spec;
        spec.Width = 2048; 
        spec.Height = 2048;
        spec.Samples = 1;
        // 只需要深度附件
        spec.Attachments = { FramebufferTextureFormat::Depth }; 
        m_ShadowMapFBO = Framebuffer::Create(spec);
    }

    void ShadowPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        
        auto lightView = context.ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();
        if (lightView.begin() == lightView.end()) {
            context.Set("ShadowMap_Output", (uint32_t)0);
            return;
        }

        // 1. 计算光照矩阵 (简化的定向光包围盒计算)
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

        // 2. 准备阴影 FBO 与管线状态
        m_ShadowMapFBO->Bind();
        cmd.SetViewport(0, 0, 2048, 2048);
        cmd.SetDepthTest(true);
        cmd.Clear(); // 只需要清空深度
        
        // 开启正面剔除，解决彼得潘悬浮问题！
        cmd.SetCullFaceFront(); 

        m_ShadowShader->Bind();
        context.Stats.ShaderBinds++;
        m_ShadowShader->SetMat4("u_LightSpaceMatrix", lightSpaceMatrix);

        // 3. 收集并渲染所有产生阴影的物体
        auto meshView = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshView) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;
            
            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            if (!meshComp.ModelAsset || !meshComp.CastShadows) continue; 

            // 提交 Transform
            m_ShadowShader->SetMat4("u_Transform", entity.GetWorldTransform());
            
            for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                std::string tag = entity.GetComponent<TagComponent>().Tag;
                uint32_t tris = mesh->GetIndexCount() / 3;

                if (context.RecordAndCheckDrawCall("Shadow Pass", tag, "Shadow Map", tris)) {
                    // 交给 cmd 执行底层绘制
                    cmd.DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
                }
            }
        }
        
        // 4. 恢复管线状态
        cmd.SetCullFaceBack(); 
        m_ShadowMapFBO->Unbind();
        
        uint32_t vpWidth = context.Get<uint32_t>("ViewportWidth", 1280);
        uint32_t vpHeight = context.Get<uint32_t>("ViewportHeight", 720);
        cmd.SetViewport(0, 0, vpWidth, vpHeight);

        // 5. 产出数据交接
        context.Set("ShadowMap_Output", m_ShadowMapFBO->GetDepthAttachmentRendererID());
        context.Set("LightSpaceMatrix", lightSpaceMatrix);
    }
}