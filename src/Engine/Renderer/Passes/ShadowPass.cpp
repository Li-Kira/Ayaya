#include "ayapch.h"
#include "ShadowPass.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    ShadowPass::ShadowPass() {
        m_PassName = "Shadow Map Pass";
    }

    ShadowPass::~ShadowPass() {
        if (m_ShadowMapFBO != 0) glDeleteFramebuffers(1, &m_ShadowMapFBO);
        if (m_ShadowMapTexture != 0) glDeleteTextures(1, &m_ShadowMapTexture);
    }

    void ShadowPass::OnAttach() {
        m_ShadowShader = Shader::Create("assets/Editor/shaders/Shadow/shadow_map.vert", "assets/Editor/shaders/Shadow/shadow_map.frag");
        
        // 【核心修复】：还原原生 OpenGL，精确控制阴影 FBO 的不写颜色与边缘纯白
        glGenFramebuffers(1, &m_ShadowMapFBO);
        glGenTextures(1, &m_ShadowMapTexture);
        glBindTexture(GL_TEXTURE_2D, m_ShadowMapTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 2048, 2048, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowMapFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_ShadowMapTexture, 0);
        glDrawBuffer(GL_NONE); 
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void ShadowPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        bool hasDirLight = context.Get<bool>("HasDirLight", false);
        if (!hasDirLight) {
            context.Set("ShadowMap_Output", (uint32_t)0);
            return;
        }

        // 1. 获取光源方向并计算光照空间矩阵
        glm::vec3 lightDir = glm::normalize(context.Get<glm::vec3>("DirLightDir", glm::vec3(0.0f, -1.0f, 0.0f)));
        glm::vec3 lightPos = -lightDir * 30.0f; 
        
        glm::mat4 lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 100.0f);
        glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightSpaceMatrix = lightProjection * lightView;

        // 2. 绑定 Shader 与 FBO
        glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowMapFBO); 
        cmd.SetViewport(0, 0, 2048, 2048);
        cmd.SetDepthTest(true);
        glClear(GL_DEPTH_BUFFER_BIT); 
        
        // 【核心修复】：还原为背面剔除，防止平面物体不产生阴影！
        cmd.SetCullFaceBack(); 

        m_ShadowShader->Bind();
        context.Stats.ShaderBinds++;
        m_ShadowShader->SetMat4("u_LightSpaceMatrix", lightSpaceMatrix);

        // 3. 收集并渲染场景中产生阴影的物体
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
        
        // 4. 恢复状态
        cmd.SetCullFaceBack(); 
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        uint32_t vpWidth = context.Get<uint32_t>("ViewportWidth", 1280);
        uint32_t vpHeight = context.Get<uint32_t>("ViewportHeight", 720);
        cmd.SetViewport(0, 0, vpWidth, vpHeight);

        // 5. 产出交接给黑板
        context.Set("ShadowMap_Output", m_ShadowMapTexture);
        context.Set("LightSpaceMatrix", lightSpaceMatrix);
    }
}