#include "ayapch.h"
#include "LightingPass.hpp"
#include "Asset/AssetManager.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Ayaya {

    LightingPass::LightingPass() {
        m_PassName = "Lighting & Forward Pass";
    }

    void LightingPass::OnAttach() {
        m_EmptyVAO.reset(VertexArray::Create());

        m_DeferredLightingShader = Shader::Create("assets/Editor/shaders/Deferred/deferred_lighting.vert", "assets/Editor/shaders/Deferred/deferred_lighting.frag");
        m_SkyboxShader           = Shader::Create("assets/Editor/shaders/Skybox/skybox.vert", "assets/Editor/shaders/Skybox/skybox.frag");
        m_GridShader             = Shader::Create("assets/Editor/shaders/UI/grid.vert", "assets/Editor/shaders/UI/grid.frag");
        m_SpriteShader           = Shader::Create("assets/Editor/shaders/2D/sprite.vert", "assets/Editor/shaders/2D/sprite.frag");
        m_OutlineShader          = Shader::Create("assets/Editor/shaders/UI/outline.vert", "assets/Editor/shaders/UI/outline.frag");

        m_DeferredLightingShader->BindUniformBlock("Camera", 0);
        m_DeferredLightingShader->BindUniformBlock("LightData", 1);
        m_SkyboxShader->BindUniformBlock("Camera", 0);
        m_GridShader->BindUniformBlock("Camera", 0);
        m_OutlineShader->BindUniformBlock("Camera", 0);

        FramebufferSpecification lightSpec;
        lightSpec.Samples = 1; 
        lightSpec.Width = 1280; lightSpec.Height = 720;
        lightSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        m_LightingFBO = Framebuffer::Create(lightSpec);

        FramebufferSpecification selSpec;
        selSpec.Samples = 1; 
        selSpec.Width = 1280; selSpec.Height = 720;
        selSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_SelectionFBO = Framebuffer::Create(selSpec);

        // 配置各子通道的 PSO 管线
        PipelineSpecification defSpec;
        defSpec.Shader = m_DeferredLightingShader;
        defSpec.TargetFramebuffer = m_LightingFBO;
        defSpec.DepthTest = false; 
        defSpec.Blend = false;
        m_DeferredPipeline = Pipeline::Create(defSpec);

        PipelineSpecification skySpec;
        skySpec.Shader = m_SkyboxShader;
        skySpec.TargetFramebuffer = m_LightingFBO;
        skySpec.DepthTest = true;
        skySpec.DepthWrite = false;
        skySpec.DepthOperator = DepthCompareOperator::LEqual; 
        skySpec.BackfaceCulling = CullMode::None;
        m_SkyboxPipeline = Pipeline::Create(skySpec);

        PipelineSpecification gridSpec;
        gridSpec.Shader = m_GridShader;
        gridSpec.TargetFramebuffer = m_LightingFBO;
        gridSpec.DepthTest = true;
        gridSpec.DepthWrite = false;
        gridSpec.Blend = true;
        gridSpec.BlendMode = BlendMode::Alpha;
        gridSpec.BackfaceCulling = CullMode::None;
        m_GridPipeline = Pipeline::Create(gridSpec);

        PipelineSpecification spriteSpec;
        spriteSpec.Shader = m_SpriteShader;
        spriteSpec.TargetFramebuffer = m_LightingFBO;
        spriteSpec.DepthTest = true;
        spriteSpec.DepthWrite = false;
        spriteSpec.Blend = true;
        spriteSpec.BlendMode = BlendMode::Alpha;
        spriteSpec.BackfaceCulling = CullMode::None;
        m_SpritePipeline = Pipeline::Create(spriteSpec);

        PipelineSpecification selMeshSpec;
        selMeshSpec.Shader = m_OutlineShader;
        selMeshSpec.TargetFramebuffer = m_SelectionFBO;
        selMeshSpec.DepthTest = false;
        selMeshSpec.Blend = false;
        m_SelectionMeshPipeline = Pipeline::Create(selMeshSpec);

        PipelineSpecification selSpriteSpec;
        selSpriteSpec.Shader = m_SpriteShader;
        selSpriteSpec.TargetFramebuffer = m_SelectionFBO;
        selSpriteSpec.DepthTest = false;
        selSpriteSpec.Blend = false;
        m_SelectionSpritePipeline = Pipeline::Create(selSpriteSpec);
    }

    void LightingPass::OnResize(uint32_t width, uint32_t height) {
        m_LightingFBO->Resize(width, height);
        m_SelectionFBO->Resize(width, height);
    }

    void LightingPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t gPosition   = context.Get<uint32_t>("GBuffer_Position", 0);
        uint32_t gNormal     = context.Get<uint32_t>("GBuffer_Normal", 0);
        uint32_t gAlbedo     = context.Get<uint32_t>("GBuffer_Albedo", 0);
        uint32_t gPBR        = context.Get<uint32_t>("GBuffer_PBR", 0);
        uint32_t gCustomData = context.Get<uint32_t>("GBuffer_CustomData", 0); 
        
        if (gPosition == 0) return;

        float physicalExposure = context.Get<float>("PhysicalExposure", 1.0f);
        glm::vec4 clearColor   = context.Get<glm::vec4>("ClearColor", glm::vec4(0.06f, 0.06f, 0.065f, 1.0f));

        // 1. 延迟光照合成 (Deferred Lighting)
        m_LightingFBO->Bind();
        cmd.SetViewport(0, 0, m_LightingFBO->GetSpecification().Width, m_LightingFBO->GetSpecification().Height);
        
        // 【完美规范】：先绑管线，再清屏！
        cmd.BindPipeline(m_DeferredPipeline);
        context.Stats.ShaderBinds++;

        glm::vec4 hdrClearColor = clearColor;
        hdrClearColor.r /= physicalExposure;
        hdrClearColor.g /= physicalExposure;
        hdrClearColor.b /= physicalExposure;
        cmd.SetClearColor(hdrClearColor);
        cmd.Clear();

        cmd.BindTexture2D(0, gPosition);   m_DeferredLightingShader->SetInt("g_Position", 0);
        cmd.BindTexture2D(1, gNormal);     m_DeferredLightingShader->SetInt("g_Normal", 1);
        cmd.BindTexture2D(2, gAlbedo);     m_DeferredLightingShader->SetInt("g_Albedo", 2);
        cmd.BindTexture2D(3, gPBR);        m_DeferredLightingShader->SetInt("g_PBR", 3);
        cmd.BindTexture2D(4, gCustomData); m_DeferredLightingShader->SetInt("g_CustomData", 4);

        if (context.Get<bool>("HasDirLight", false)) {
            cmd.BindTexture2D(5, context.Get<uint32_t>("ShadowMap_Output", 0));
            m_DeferredLightingShader->SetInt("u_ShadowMap", 5); 
            m_DeferredLightingShader->SetMat4("u_LightSpaceMatrix", context.Get<glm::mat4>("LightSpaceMatrix"));
        }

        auto irrMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap", nullptr);
        auto preMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap", nullptr);
        if (irrMap && preMap) {
            irrMap->Bind(6); m_DeferredLightingShader->SetInt("u_IrradianceMap", 6);
            preMap->Bind(7); m_DeferredLightingShader->SetInt("u_PrefilteredMap", 7);
            m_DeferredLightingShader->SetBool("u_EnvMapEnabled", true);
            m_DeferredLightingShader->SetFloat("u_Intensity", context.Get<float>("EnvironmentIntensity", 1.0f));
        } else {
            m_DeferredLightingShader->SetBool("u_EnvMapEnabled", false);
        }
        m_DeferredLightingShader->SetFloat3("u_AmbientColor", context.Get<glm::vec3>("EnvironmentAmbientColor", glm::vec3(0.1f)) * context.Get<float>("EnvironmentIntensity", 1.0f));

        auto brdfLUT = context.GetTexture("BRDFLUT");
        if (brdfLUT) {
            brdfLUT->Bind(8); m_DeferredLightingShader->SetInt("u_BRDFLUT", 8); 
        }

        if (context.RecordAndCheckDrawCall("Lighting Pass", "Deferred Combine", "DeferredLighting Shader", 2)) {
            cmd.DrawArrays(m_EmptyVAO, 3);
        }

        // 2. 深度拷贝 (Blit Depth)
        auto geoFBO = context.Framebuffers["Geometry"];
        if (geoFBO) {
            uint32_t width = geoFBO->GetSpecification().Width;
            uint32_t height = geoFBO->GetSpecification().Height;
            cmd.BlitDepth(geoFBO->GetRendererID(), m_LightingFBO->GetRendererID(), width, height);
            m_LightingFBO->Bind();
        }

        // 3. 正向渲染 (Forward Pass)
        // 3.1 渲染天空盒
        if (context.Get<bool>("ShowSkybox", false)) {
            auto envMap = context.Get<std::shared_ptr<TextureCube>>("EnvironmentCubemap", nullptr);
            auto skyMesh = context.Get<std::shared_ptr<Mesh>>("SkyboxMesh", nullptr);
            if (envMap && skyMesh) {
                cmd.BindPipeline(m_SkyboxPipeline);
                context.Stats.ShaderBinds++;

                glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(context.ViewMatrix));
                glm::mat4 skyProjection = context.ProjectionMatrix;
                if (skyProjection[3][3] == 1.0f) {
                    float aspect = (float)m_LightingFBO->GetSpecification().Width / (float)m_LightingFBO->GetSpecification().Height;
                    skyProjection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
                }
                
                m_SkyboxShader->SetMat4("u_View", viewNoTranslation);
                m_SkyboxShader->SetMat4("u_Projection", skyProjection);
                m_SkyboxShader->SetFloat("u_Intensity", context.Get<float>("EnvironmentIntensity", 1.0f));
                
                envMap->Bind(0); 
                m_SkyboxShader->SetInt("u_Skybox", 0); 

                if (context.RecordAndCheckDrawCall("Lighting Pass", "Skybox", "Skybox Shader", skyMesh->GetIndexCount() / 3)) {
                    m_SkyboxShader->SetMat4("u_Transform", glm::mat4(1.0f));
                    cmd.DrawIndexed(skyMesh->GetVertexArray(), skyMesh->GetIndexCount());
                }
            }
        }

        // 3.2 渲染网格地基
        if (context.Get<bool>("ShowGrid", false)) {
            cmd.BindPipeline(m_GridPipeline);
            context.Stats.ShaderBinds++;

            m_GridShader->SetFloat("u_ExposureInverse", 1.0f / physicalExposure);
            glm::mat4 gridTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1000.0f, 1.0f, 1000.0f));
            m_GridShader->SetMat4("u_Transform", gridTransform);
            
            auto gridMesh = context.Get<std::shared_ptr<Mesh>>("GridMesh", nullptr);
            if (gridMesh) {
                if (context.RecordAndCheckDrawCall("Lighting Pass", "Grid", "Grid Shader", gridMesh->GetIndexCount() / 3)) {
                    cmd.DrawIndexed(gridMesh->GetVertexArray(), gridMesh->GetIndexCount());
                }
            }
        }

        // 3.3 渲染 2D 纸片人 (Sprites)
        {
            cmd.BindPipeline(m_SpritePipeline);
            context.Stats.ShaderBinds++;

            m_SpriteDrawList.clear();
            auto spriteGroup = context.ActiveScene->Reg().view<TransformComponent, SpriteRendererComponent>();
            for (auto entityID : spriteGroup) {
                Entity entity{ entityID, context.ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue; 

                auto [transformComp, spriteComp] = spriteGroup.get<TransformComponent, SpriteRendererComponent>(entityID);
                glm::mat4 transform = entity.GetWorldTransform();
                float distance = glm::length(context.CameraPosition - glm::vec3(transform[3]));
                m_SpriteDrawList.push_back({ transform, spriteComp, distance });
            }

            std::sort(m_SpriteDrawList.begin(), m_SpriteDrawList.end(), [](const SpriteDrawCommand& a, const SpriteDrawCommand& b) {
                return a.DistanceToCamera > b.DistanceToCamera; 
            });
            
            for (const auto& drawCmd : m_SpriteDrawList) {
                m_SpriteShader->SetMat4("u_Transform", drawCmd.Transform);
                m_SpriteShader->SetFloat4("u_Color", drawCmd.SpriteComp.Color);

                if (drawCmd.SpriteComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(drawCmd.SpriteComp.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(drawCmd.SpriteComp.TextureHandle);
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
                if (context.RecordAndCheckDrawCall("Lighting Pass", "Sprite", "Sprite Shader", 2)) {
                    cmd.DrawTriangleStrip(m_EmptyVAO, 4);
                }
            }
        }

        m_LightingFBO->Unbind();

        // 4. 选择轮廓描边 (Selection Pass)
        m_SelectionFBO->Bind();
        cmd.SetViewport(0, 0, m_SelectionFBO->GetSpecification().Width, m_SelectionFBO->GetSpecification().Height);
        
        // 【完美规范】：先绑管线，再清屏！
        cmd.BindPipeline(m_SelectionMeshPipeline);
        
        cmd.SetClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        cmd.Clear();

        Entity hoveredEntity = context.Get<Entity>("HoveredEntity", Entity{});
        if (hoveredEntity && hoveredEntity.IsActiveInHierarchy()) {
            glm::mat4 transform = hoveredEntity.GetWorldTransform();

            if (hoveredEntity.HasComponent<MeshRendererComponent>()) {
                // 已在上文 BindPipeline
                context.Stats.ShaderBinds++;

                m_OutlineShader->SetFloat3("u_Color", glm::vec3(1.0f, 1.0f, 1.0f)); 
                auto& meshComp = hoveredEntity.GetComponent<MeshRendererComponent>();
                if (meshComp.ModelAsset) {
                    m_OutlineShader->SetMat4("u_Transform", transform);
                    for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                        cmd.DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
                    }
                }
            }
            else if (hoveredEntity.HasComponent<SpriteRendererComponent>()) {
                cmd.BindPipeline(m_SelectionSpritePipeline);
                context.Stats.ShaderBinds++;

                auto& spriteComp = hoveredEntity.GetComponent<SpriteRendererComponent>();
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

                cmd.DrawTriangleStrip(m_EmptyVAO, 4);
            }
        }
        
        m_SelectionFBO->Unbind();

        // 5. 产出数据交接
        context.Set("Lighting_Output", m_LightingFBO->GetColorAttachmentRendererID(0));
        context.Set("Selection_Output", m_SelectionFBO->GetColorAttachmentRendererID(0));
        context.Framebuffers["Lighting"] = m_LightingFBO;
        context.Framebuffers["Selection"] = m_SelectionFBO;
    }
}