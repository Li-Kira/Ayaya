#include "ayapch.h"
#include "SceneRenderer.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/RenderCommand.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Mesh.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Texture.hpp"
#include "Engine/Scene/Components.hpp"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Ayaya {

    // 使用静态结构体管理管线内部的资源，不对外暴露
    struct SceneRendererData {
        glm::mat4 ViewProjectionMatrix;
        
        std::shared_ptr<Texture2D> WhiteTexture;
        std::shared_ptr<Mesh> GridMesh;
        
        std::shared_ptr<Shader> DefaultShader;
        std::shared_ptr<Shader> OutlineShader;
        std::shared_ptr<Shader> GridShader;
    };

    static SceneRendererData s_Data;

    void SceneRenderer::Init() {
        // 1. 初始化纯白贴图
        s_Data.WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff; 
        s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        // 2. 初始化 3D 网格地基
        s_Data.GridMesh = Mesh::CreateCube(1.0f);

        // 3. 加载管线必需的 Shader
        // 注意：这里需要确保你的 Shader 类有 Create 方法，或者你继续使用 ShaderLibrary。
        // 为了简便，我们直接使用 Shader::Create
        s_Data.DefaultShader = Shader::Create("assets/shaders/lighting.vert", "assets/shaders/lighting.frag");
        s_Data.OutlineShader = Shader::Create("assets/shaders/outline.vert", "assets/shaders/outline.frag");
        s_Data.GridShader    = Shader::Create("assets/shaders/grid.vert", "assets/shaders/grid.frag");
    }

    void SceneRenderer::BeginScene(const glm::mat4& viewProjection) {
        s_Data.ViewProjectionMatrix = viewProjection;
        Renderer::BeginScene(viewProjection);
    }

    void SceneRenderer::EndScene() {
        Renderer::EndScene();
    }

    void SceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene, Entity hoveredEntity, bool showGrid) {
        
        // ==========================================
        // Pass 1: Background Grid Pass (渲染无限网格)
        // ==========================================
        if (showGrid) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);

            s_Data.GridShader->Bind();
            glm::mat4 gridTransform = glm::scale(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 100.0f));
            Renderer::Submit(s_Data.GridShader, s_Data.GridMesh->GetVertexArray(), gridTransform);
            
            glDepthMask(GL_TRUE); 
        }

        // ==========================================
        // Pass 2: Lighting Setup Pass (收集场景灯光)
        // ==========================================
        glm::vec3 lightDir = { -0.2f, -1.0f, -0.3f }; 
        glm::vec3 lightColor = { 1.0f, 1.0f, 1.0f };
        float ambientStrength = 0.3f;

        auto lightGroup = scene->Reg().view<TransformComponent, DirectionalLightComponent>();
        for (auto entityID : lightGroup) {
            Entity lightEntity{ entityID, scene.get() };
            auto& transform = lightEntity.GetComponent<TransformComponent>();
            auto& dlc = lightEntity.GetComponent<DirectionalLightComponent>();

            glm::quat orientation = glm::quat(transform.Rotation);
            lightDir = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
            lightColor = dlc.Color;
            ambientStrength = dlc.AmbientStrength;
            break; // 暂只支持一盏主平行光
        }

        s_Data.DefaultShader->Bind();
        s_Data.DefaultShader->SetInt("u_Texture", 0);
        s_Data.DefaultShader->SetFloat3("u_LightDir", lightDir); 
        s_Data.DefaultShader->SetFloat3("u_LightColor", lightColor);
        s_Data.DefaultShader->SetFloat("u_AmbientStrength", ambientStrength);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
        glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);

        // ==========================================
        // Pass 3: Geometry Pass (渲染所有 3D 网格)
        // ==========================================
        auto meshGroup = scene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshGroup) {
            Entity entity{ entityID, scene.get() };
            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            
            // 悬停描边遮罩
            if (hoveredEntity && hoveredEntity == entity) {
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glStencilMask(0xFF); 
            } else {
                glStencilFunc(GL_ALWAYS, 0, 0xFF);
                glStencilMask(0x00); 
            }

            if (meshComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(meshComp.TextureHandle)) {
                auto tex = AssetManager::GetAsset<Texture2D>(meshComp.TextureHandle);
                tex->Bind(0);
            } else {
                s_Data.WhiteTexture->Bind(0);
            }

            s_Data.DefaultShader->SetFloat3("u_ColorModifier", glm::vec3(meshComp.Color)); 
            
            if (meshComp.ModelAsset) {
                // 遍历模型里的所有子网格并提交渲染
                for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                    Renderer::Submit(s_Data.DefaultShader, mesh->GetVertexArray(), entity.GetWorldTransform());
                }
            }
        }

        // ==========================================
        // Pass 4: Outline Post-Pass (渲染描边高亮)
        // ==========================================
        if (hoveredEntity && hoveredEntity.HasComponent<MeshRendererComponent>()) {
            glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
            glStencilMask(0x00);      
            glDisable(GL_DEPTH_TEST); 

            s_Data.OutlineShader->Bind();
            s_Data.OutlineShader->SetFloat3("u_Color", glm::vec3(1.0f, 0.65f, 0.0f)); 
            
            glm::mat4 transform = hoveredEntity.GetWorldTransform();
            transform = transform * glm::scale(glm::mat4(1.0f), glm::vec3(1.05f)); 
            
            auto& meshComp = hoveredEntity.GetComponent<MeshRendererComponent>();
            if (meshComp.ModelAsset) {
                for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                    Renderer::Submit(s_Data.OutlineShader, mesh->GetVertexArray(), transform);
                }
            }

            glStencilMask(0xFF);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
        }
    }
}