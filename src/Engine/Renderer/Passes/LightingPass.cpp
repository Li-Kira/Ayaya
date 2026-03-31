#include "ayapch.h"
#include "LightingPass.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/RenderCommand.hpp"
#include "Renderer/Renderer.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Ayaya {

    LightingPass::LightingPass() {
        m_PassName = "Lighting & Forward Pass";
    }

    LightingPass::~LightingPass() {
        if (m_EmptyVAO != 0) glDeleteVertexArrays(1, &m_EmptyVAO);
    }

    void LightingPass::OnAttach() {
        glGenVertexArrays(1, &m_EmptyVAO);

        // 1. 加载所有涉及光照与正向渲染的 Shader
        m_DeferredLightingShader = Shader::Create("assets/Editor/shaders/Deferred/deferred_lighting.vert", "assets/Editor/shaders/Deferred/deferred_lighting.frag");
        m_SkyboxShader           = Shader::Create("assets/Editor/shaders/Skybox/skybox.vert", "assets/Editor/shaders/Skybox/skybox.frag");
        m_GridShader             = Shader::Create("assets/Editor/shaders/UI/grid.vert", "assets/Editor/shaders/UI/grid.frag");
        m_SpriteShader           = Shader::Create("assets/Editor/shaders/2D/sprite.vert", "assets/Editor/shaders/2D/sprite.frag");
        m_OutlineShader          = Shader::Create("assets/Editor/shaders/UI/outline.vert", "assets/Editor/shaders/UI/outline.frag");

        // 绑定 UBO
        m_DeferredLightingShader->BindUniformBlock("Camera", 0);
        m_DeferredLightingShader->BindUniformBlock("LightData", 1);
        m_SkyboxShader->BindUniformBlock("Camera", 0);
        m_GridShader->BindUniformBlock("Camera", 0);
        m_OutlineShader->BindUniformBlock("Camera", 0);

        // 2. 创建 Lighting FBO (合成光照，HDR 精度)
        FramebufferSpecification lightSpec;
        lightSpec.Samples = 1; 
        lightSpec.Width = 1280; lightSpec.Height = 720;
        lightSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        m_LightingFBO = Framebuffer::Create(lightSpec);

        // 3. 创建 Selection FBO (选中物体的纯色剪影)
        FramebufferSpecification selSpec;
        selSpec.Samples = 1; 
        selSpec.Width = 1280; selSpec.Height = 720;
        selSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_SelectionFBO = Framebuffer::Create(selSpec);
    }

    void LightingPass::OnResize(uint32_t width, uint32_t height) {
        m_LightingFBO->Resize(width, height);
        m_SelectionFBO->Resize(width, height);
    }

    void LightingPass::Execute(RenderContext& context) {
        // ==========================================
        // 0. 从黑板提取前置依赖数据
        // ==========================================
        uint32_t gPosition = context.Get<uint32_t>("GBuffer_Position", 0);
        uint32_t gNormal   = context.Get<uint32_t>("GBuffer_Normal", 0);
        uint32_t gAlbedo   = context.Get<uint32_t>("GBuffer_Albedo", 0);
        uint32_t gPBR      = context.Get<uint32_t>("GBuffer_PBR", 0);
        
        // 如果前面 G-Buffer 没干活，本 Pass 直接罢工
        if (gPosition == 0) return;

        float physicalExposure = context.Get<float>("PhysicalExposure", 1.0f);
        glm::vec4 clearColor   = context.Get<glm::vec4>("ClearColor", {0.06f, 0.06f, 0.065f, 1.0f});

        // ==========================================
        // 1. 延迟光照核爆合成 (Deferred Lighting)
        // ==========================================
        m_LightingFBO->Bind();
        
        // 为背景色注入物理能量，抵消后期曝光
        glm::vec4 hdrClearColor = clearColor;
        hdrClearColor.r /= physicalExposure;
        hdrClearColor.g /= physicalExposure;
        hdrClearColor.b /= physicalExposure;

        RenderCommand::SetClearColor(hdrClearColor);
        RenderCommand::Clear();

        // 画全屏四边形合成光照，绝对不能开启深度测试！
        glDisable(GL_DEPTH_TEST);
        m_DeferredLightingShader->Bind();
        context.Stats.ShaderBinds++;

        // 绑定 G-Buffer 贴图
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gPosition); m_DeferredLightingShader->SetInt("g_Position", 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gNormal);   m_DeferredLightingShader->SetInt("g_Normal", 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, gAlbedo);   m_DeferredLightingShader->SetInt("g_Albedo", 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, gPBR);      m_DeferredLightingShader->SetInt("g_PBR", 3);

        // 绑定 IBL 漫反射与高光贴图
        auto irrMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap");
        auto preMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
        if (irrMap && preMap) {
            irrMap->Bind(4); m_DeferredLightingShader->SetInt("u_IrradianceMap", 4);
            preMap->Bind(5); m_DeferredLightingShader->SetInt("u_PrefilteredMap", 5);
            m_DeferredLightingShader->SetBool("u_EnvMapEnabled", true);
            m_DeferredLightingShader->SetFloat("u_Intensity", context.Get<float>("EnvironmentIntensity", 1.0f));
        } else {
            m_DeferredLightingShader->SetBool("u_EnvMapEnabled", false);
        }
        
        m_DeferredLightingShader->SetFloat3("u_AmbientColor", context.Get<glm::vec3>("EnvironmentAmbientColor") * context.Get<float>("EnvironmentIntensity", 1.0f));

        auto brdfLUT = context.GetTexture("BRDFLUT");
        if (brdfLUT) {
            brdfLUT->Bind(6); m_DeferredLightingShader->SetInt("u_BRDFLUT", 6);
        }

        // 绑定阴影贴图
        if (context.Get<bool>("HasDirLight", false)) {
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, context.Get<uint32_t>("ShadowMap", 0));
            m_DeferredLightingShader->SetInt("u_ShadowMap", 7);
            m_DeferredLightingShader->SetMat4("u_LightSpaceMatrix", context.Get<glm::mat4>("LightSpaceMatrix"));
        }

        // 轰炸全屏
        glBindVertexArray(m_EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        context.Stats.DrawCalls++; context.Stats.TriangleCount += 1; context.Stats.VertexCount += 3;
        
        glEnable(GL_DEPTH_TEST); // 画完恢复深度测试

        // ==========================================
        // 2. 深度拷贝 (Blit Depth)
        // 把 G-Buffer 的深度抄过来，供后续 Forward Pass 遮挡使用
        // ==========================================
        auto geoFBO = context.Framebuffers["Geometry"];
        if (geoFBO) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, geoFBO->GetRendererID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_LightingFBO->GetRendererID());
            uint32_t width = geoFBO->GetSpecification().Width;
            uint32_t height = geoFBO->GetSpecification().Height;
            glBlitFramebuffer(0, 0, width, height,
                              0, 0, width, height,
                              GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, m_LightingFBO->GetRendererID());
        }

        // ==========================================
        // 3. 正向渲染 (Forward Pass)
        // ==========================================
        
        // 3.1 渲染天空盒
        if (context.Get<bool>("ShowSkybox", false)) {
            auto envMap = context.Get<std::shared_ptr<TextureCube>>("EnvironmentCubemap");
            auto skyMesh = context.Get<std::shared_ptr<Mesh>>("SkyboxMesh");
            if (envMap && skyMesh) {
                glDepthFunc(GL_LEQUAL);  
                m_SkyboxShader->Bind();
                context.Stats.ShaderBinds++;
                m_SkyboxShader->SetFloat("u_Intensity", context.Get<float>("EnvironmentIntensity", 1.0f));
                
                glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(context.ViewMatrix));
                m_SkyboxShader->SetMat4("u_View", viewNoTranslation);
                
                glm::mat4 skyProjection = context.ProjectionMatrix;
                // 修复正交相机下天空盒消失的问题
                if (skyProjection[3][3] == 1.0f) {
                    float aspect = (float)m_LightingFBO->GetSpecification().Width / (float)m_LightingFBO->GetSpecification().Height;
                    skyProjection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
                }
                m_SkyboxShader->SetMat4("u_Projection", skyProjection);
                
                envMap->Bind(0); 
                m_SkyboxShader->SetInt("u_Skybox", 0);

                glDisable(GL_CULL_FACE); 
                Renderer::Submit(m_SkyboxShader, skyMesh->GetVertexArray(), glm::mat4(1.0f));
                context.Stats.DrawCalls++; context.Stats.TriangleCount += 12; context.Stats.VertexCount += 36;
                glEnable(GL_CULL_FACE);
                glDepthFunc(GL_LESS);
            }
        }

        // 3.2 渲染网格地基
        if (context.Get<bool>("ShowGrid", false)) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE); 
            glDisable(GL_CULL_FACE);
            
            m_GridShader->Bind();
            m_GridShader->SetFloat("u_ExposureInverse", 1.0f / physicalExposure);
            
            glm::mat4 gridTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1000.0f, 1.0f, 1000.0f));
            Renderer::Submit(m_GridShader, context.Get<std::shared_ptr<Mesh>>("GridMesh")->GetVertexArray(), gridTransform);
            
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE); 
            glDisable(GL_BLEND); 
        }

        // 3.3 渲染 2D 纸片人 (Sprites) - 采用 Painter's Algorithm 透明度排序
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE); // 防止透明物体的黑边互相遮挡
            glDisable(GL_CULL_FACE); 

            m_SpriteShader->Bind();
            context.Stats.ShaderBinds++;

            m_SpriteDrawList.clear();

            auto spriteGroup = context.ActiveScene->Reg().view<TransformComponent, SpriteRendererComponent>();
            for (auto entityID : spriteGroup) {
                Entity entity{ entityID, context.ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue; 

                auto [transformComp, spriteComp] = spriteGroup.get<TransformComponent, SpriteRendererComponent>(entityID);
                glm::mat4 transform = entity.GetWorldTransform();
                
                // 计算该 Sprite 距离相机的距离
                float distance = glm::length(context.CameraPosition - glm::vec3(transform[3]));
                m_SpriteDrawList.push_back({ transform, spriteComp, distance });
            }

            // 由远及近排序！(距离大的排在前面)
            std::sort(m_SpriteDrawList.begin(), m_SpriteDrawList.end(), [](const SpriteDrawCommand& a, const SpriteDrawCommand& b) {
                return a.DistanceToCamera > b.DistanceToCamera; 
            });

            glBindVertexArray(m_EmptyVAO); 
            auto whiteTex = context.GetTexture("WhiteTexture");
            
            for (const auto& cmd : m_SpriteDrawList) {
                m_SpriteShader->SetMat4("u_Transform", cmd.Transform);
                m_SpriteShader->SetFloat4("u_Color", cmd.SpriteComp.Color);

                if (cmd.SpriteComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(cmd.SpriteComp.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(cmd.SpriteComp.TextureHandle);
                    tex->Bind(0);
                    m_SpriteShader->SetInt("u_Texture", 0);
                    m_SpriteShader->SetBool("u_UseTexture", true);
                } else {
                    auto whiteTex = context.GetTexture("WhiteTexture");
                    if(whiteTex) whiteTex->Bind(0);
                    m_SpriteShader->SetInt("u_Texture", 0);
                    m_SpriteShader->SetBool("u_UseTexture", false);
                }

                m_SpriteShader->SetFloat("u_ExposureInverse", 1.0f / physicalExposure);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                context.Stats.DrawCalls++; context.Stats.TriangleCount += 2; context.Stats.VertexCount += 4;
            }

            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        m_LightingFBO->Unbind();

        // ==========================================
        // 4. 选择轮廓描边 (Selection Pass)
        // ==========================================
        m_SelectionFBO->Bind();
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        RenderCommand::Clear();

        Entity hoveredEntity = context.Get<Entity>("HoveredEntity");
        if (hoveredEntity && hoveredEntity.IsActiveInHierarchy()) {
            
            glDisable(GL_DEPTH_TEST); // X-Ray 穿透墙壁效果

            glm::mat4 transform = hoveredEntity.GetWorldTransform();

            // 4.1: 3D 网格模型的纯白剪影
            if (hoveredEntity.HasComponent<MeshRendererComponent>()) {
                m_OutlineShader->Bind();
                m_OutlineShader->SetFloat3("u_Color", glm::vec3(1.0f, 1.0f, 1.0f)); 

                auto& meshComp = hoveredEntity.GetComponent<MeshRendererComponent>();
                if (meshComp.ModelAsset) {
                    for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                        Renderer::Submit(m_OutlineShader, mesh->GetVertexArray(), transform);
                    }
                }
            }
            // 4.2: 2D 精灵图的像素级剪影
            else if (hoveredEntity.HasComponent<SpriteRendererComponent>()) {
                auto& spriteComp = hoveredEntity.GetComponent<SpriteRendererComponent>();

                m_SpriteShader->Bind();
                m_SpriteShader->SetMat4("u_Transform", transform);
                m_SpriteShader->SetFloat4("u_Color", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
                m_SpriteShader->SetFloat("u_ExposureInverse", 1.0f);

                if (spriteComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(spriteComp.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(spriteComp.TextureHandle);
                    tex->Bind(0);
                    m_SpriteShader->SetInt("u_Texture", 0);
                    m_SpriteShader->SetBool("u_UseTexture", true);
                } else {
                    auto whiteTex = context.GetTexture("WhiteTexture");
                    if(whiteTex) whiteTex->Bind(0);
                    m_SpriteShader->SetInt("u_Texture", 0);
                    m_SpriteShader->SetBool("u_UseTexture", false);
                }

                glBindVertexArray(m_EmptyVAO);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
            glEnable(GL_DEPTH_TEST);
        }
        
        m_SelectionFBO->Unbind();

        // ==========================================
        // 5. 产出数据交接
        // 把结果挂上黑板，通知下游 (Bloom / PostProcess) 来取货
        // ==========================================
        context.Set("Lighting_Output", m_LightingFBO->GetColorAttachmentRendererID(0));
        context.Set("Selection_Output", m_SelectionFBO->GetColorAttachmentRendererID(0));
    }
}