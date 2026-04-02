#include "ayapch.h"
#include "LightingPass.hpp"
#include "Asset/AssetManager.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
// 【进化】：已经不需要包含 <glad/glad.h> 了！所有状态均由 CommandBuffer 和 PSO 代理！

namespace Ayaya {

    LightingPass::LightingPass() {
        m_PassName = "Lighting & Forward Pass";
    }

    void LightingPass::OnAttach() {
        m_EmptyVAO = VertexArray::Create();

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

        // ==========================================
        // 配置各子通道的 PSO 管线
        // ==========================================
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

        m_DeferredMaterial = std::make_shared<Material>();
        m_DeferredMaterial->Name = "Deferred Global Material";
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

        glm::vec4 hdrClearColor = clearColor;
        hdrClearColor.r /= physicalExposure;
        hdrClearColor.g /= physicalExposure;
        hdrClearColor.b /= physicalExposure;

        // ==========================================
        // 1. 延迟光照合成 (Deferred Lighting)
        // ==========================================
        cmd.BeginRenderPass(m_LightingFBO, true, hdrClearColor);
        cmd.BindPipeline(m_DeferredPipeline);
        context.Stats.ShaderBinds++;

        // 【终极优化】：像寄包裹一样把数据塞进材质，再一键提交！
        m_DeferredMaterial->SetRuntimeTexture("g_Position", gPosition);
        m_DeferredMaterial->SetRuntimeTexture("g_Normal",   gNormal);
        m_DeferredMaterial->SetRuntimeTexture("g_Albedo",   gAlbedo);
        m_DeferredMaterial->SetRuntimeTexture("g_PBR",      gPBR);
        m_DeferredMaterial->SetRuntimeTexture("g_CustomData", gCustomData);

        if (context.Get<bool>("HasDirLight", false)) {
            m_DeferredMaterial->SetRuntimeTexture("u_ShadowMap", context.Get<uint32_t>("ShadowMap_Output", 0));
            // 矩阵属于高频小数据，依旧使用推送常量
            cmd.PushConstant(m_DeferredPipeline, "u_LightSpaceMatrix", context.Get<glm::mat4>("LightSpaceMatrix"));
        }

        auto irrMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap", nullptr);
        auto preMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap", nullptr);
        if (irrMap && preMap) {
            cmd.BindTextureCube(m_DeferredPipeline, "u_IrradianceMap",  8, irrMap->GetRendererID());
            cmd.BindTextureCube(m_DeferredPipeline, "u_PrefilteredMap", 9, preMap->GetRendererID());
            
            cmd.PushConstant(m_DeferredPipeline, "u_EnvMapEnabled", 1); 
            cmd.PushConstant(m_DeferredPipeline, "u_Intensity", context.Get<float>("EnvironmentIntensity", 1.0f));
        } else {
            cmd.PushConstant(m_DeferredPipeline, "u_EnvMapEnabled", 0);
        }
        
        cmd.PushConstant(m_DeferredPipeline, "u_AmbientColor", context.Get<glm::vec3>("EnvironmentAmbientColor", glm::vec3(0.1f)) * context.Get<float>("EnvironmentIntensity", 1.0f));

        auto brdfLUT = context.GetTexture("BRDFLUT");
        if (brdfLUT) {
            m_DeferredMaterial->SetRuntimeTexture("u_BRDFLUT", brdfLUT->GetRendererID()); 
        }

        // 一键绑定材质 (自动分配上面设置的 Texture 槽位并通知 Shader)
        auto whiteTex = context.GetTexture("WhiteTexture");
        m_DeferredMaterial->Bind(cmd, m_DeferredPipeline, whiteTex ? whiteTex->GetRendererID() : 0);

        if (context.RecordAndCheckDrawCall("Lighting Pass", "Deferred Combine", "DeferredLighting Shader", 2)) {
            cmd.DrawArrays(m_EmptyVAO, 3);
        }
        cmd.EndRenderPass();

        // ==========================================
        // 2. 深度拷贝 (Blit Depth)
        // ==========================================
        auto geoFBO = context.Framebuffers["Geometry"];
        if (geoFBO) {
            uint32_t width = geoFBO->GetSpecification().Width;
            uint32_t height = geoFBO->GetSpecification().Height;
            cmd.BlitDepth(geoFBO->GetRendererID(), m_LightingFBO->GetRendererID(), width, height);
        }

        // ==========================================
        // 3. 正向渲染 (Forward Pass)
        // ==========================================
        // 【显式 RenderPass 2】：重新进入 FBO，且 LoadOp = LOAD (不清空原有像素)
        cmd.BeginRenderPass(m_LightingFBO, false);

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
                
                // 天空盒
                cmd.PushConstant(m_SkyboxPipeline, "u_View", viewNoTranslation);
                cmd.PushConstant(m_SkyboxPipeline, "u_Projection", skyProjection);
                cmd.PushConstant(m_SkyboxPipeline, "u_Intensity", context.Get<float>("EnvironmentIntensity", 1.0f));
                
                // 天空盒贴图绑定
                cmd.BindTextureCube(m_SkyboxPipeline, "u_Skybox", 0, envMap->GetRendererID()); 
                
                if (context.RecordAndCheckDrawCall("Lighting Pass", "Skybox", "Skybox Shader", skyMesh->GetIndexCount() / 3)) {
                    cmd.PushConstant(m_SkyboxPipeline, "u_Transform", glm::mat4(1.0f));
                    cmd.DrawIndexed(skyMesh->GetVertexArray(), skyMesh->GetIndexCount());
                }

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

            cmd.PushConstant(m_GridPipeline, "u_ExposureInverse", 1.0f / physicalExposure);
            glm::mat4 gridTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1000.0f, 1.0f, 1000.0f));
            cmd.PushConstant(m_GridPipeline, "u_Transform", gridTransform);
            
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
                cmd.PushConstant(m_SpritePipeline, "u_Transform", drawCmd.Transform);
                cmd.PushConstant(m_SpritePipeline, "u_Color", drawCmd.SpriteComp.Color);

                if (drawCmd.SpriteComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(drawCmd.SpriteComp.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(drawCmd.SpriteComp.TextureHandle);
                    cmd.BindTexture2D(m_SpritePipeline, "u_Texture", 0, tex->GetRendererID());
                    cmd.PushConstant(m_SpritePipeline, "u_UseTexture", 1);
                } else {
                    auto whiteTex = context.GetTexture("WhiteTexture");
                    uint32_t texID = whiteTex ? whiteTex->GetRendererID() : 0;
                    cmd.BindTexture2D(m_SpritePipeline, "u_Texture", 0, texID);
                    cmd.PushConstant(m_SpritePipeline, "u_UseTexture", 0);
                }

                cmd.PushConstant(m_SpritePipeline, "u_ExposureInverse", 1.0f / physicalExposure);
                if (context.RecordAndCheckDrawCall("Lighting Pass", "Sprite", "Sprite Shader", 2)) {
                    cmd.DrawTriangleStrip(m_EmptyVAO, 4);
                }
            }
        }

        // 【显式结束 RenderPass 2】
        cmd.EndRenderPass();

        // ==========================================
        // 4. 选择轮廓描边 (Selection Pass)
        // ==========================================
        // 【显式 RenderPass 3】：独立的描边缓冲，清空为全透明
        cmd.BeginRenderPass(m_SelectionFBO, true, glm::vec4(0.0f));

        Entity hoveredEntity = context.Get<Entity>("HoveredEntity", Entity{});
        if (hoveredEntity && hoveredEntity.IsActiveInHierarchy()) {
            glm::mat4 transform = hoveredEntity.GetWorldTransform();

            if (hoveredEntity.HasComponent<MeshRendererComponent>()) {
                cmd.BindPipeline(m_SelectionMeshPipeline);
                context.Stats.ShaderBinds++;

                // 【终极替换】：全改为 PushConstant
                cmd.PushConstant(m_SelectionMeshPipeline, "u_Color", glm::vec3(1.0f, 1.0f, 1.0f)); 
                auto& meshComp = hoveredEntity.GetComponent<MeshRendererComponent>();
                if (meshComp.ModelAsset) {
                    cmd.PushConstant(m_SelectionMeshPipeline, "u_Transform", transform);
                    for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                        cmd.DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
                    }
                }
            }
            else if (hoveredEntity.HasComponent<SpriteRendererComponent>()) {
                cmd.BindPipeline(m_SelectionSpritePipeline);
                context.Stats.ShaderBinds++;

                auto& spriteComp = hoveredEntity.GetComponent<SpriteRendererComponent>();
                // 【终极替换】：全改为 PushConstant
                cmd.PushConstant(m_SelectionSpritePipeline, "u_Transform", transform);
                cmd.PushConstant(m_SelectionSpritePipeline, "u_Color", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
                cmd.PushConstant(m_SelectionSpritePipeline, "u_ExposureInverse", 1.0f);

                if (spriteComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(spriteComp.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(spriteComp.TextureHandle);
                    cmd.BindTexture2D(m_SelectionSpritePipeline, "u_Texture", 0, tex->GetRendererID());
                    cmd.PushConstant(m_SelectionSpritePipeline, "u_UseTexture", 1);
                } else {
                    auto whiteTex = context.GetTexture("WhiteTexture");
                    cmd.BindTexture2D(m_SelectionSpritePipeline, "u_Texture", 0, whiteTex ? whiteTex->GetRendererID() : 0);
                    cmd.PushConstant(m_SelectionSpritePipeline, "u_UseTexture", 0);
                }

                cmd.DrawTriangleStrip(m_EmptyVAO, 4);
            }
        }
        
        // 【显式结束 RenderPass 3】
        cmd.EndRenderPass();

        // ==========================================
        // 5. 产出数据交接
        // ==========================================
        context.Set("Lighting_Output", m_LightingFBO->GetColorAttachmentRendererID(0));
        context.Set("Selection_Output", m_SelectionFBO->GetColorAttachmentRendererID(0));
        context.Framebuffers["Lighting"] = m_LightingFBO;
        context.Framebuffers["Selection"] = m_SelectionFBO;
    }
}