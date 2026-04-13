#include "ayapch.h"
#include "VulkanLightingPass.hpp"
#include "Asset/AssetManager.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Ayaya {

    VulkanLightingPass::VulkanLightingPass() {
        m_PassName = "Vulkan Lighting & Forward Pass";
    }

    void VulkanLightingPass::OnAttach() {
        // 加载着色器
        m_DeferredLightingShader = Shader::Create("Deferred/deferred_lighting.vert", "Deferred/deferred_lighting.frag");
        m_SkyboxShader           = Shader::Create("Skybox/skybox.vert",              "Skybox/skybox.frag");
        m_GridShader             = Shader::Create("UI/grid.vert",                    "UI/grid.frag");
        m_SpriteShader           = Shader::Create("2D/sprite.vert",                  "2D/sprite.frag");
        m_OutlineShader          = Shader::Create("UI/outline.vert",                 "UI/outline.frag");

        // 绑定 UBO (Vulkan中通常架空，保留以兼容)
        m_DeferredLightingShader->BindUniformBlock("Camera", 0);
        m_DeferredLightingShader->BindUniformBlock("LightData", 1);
        m_SkyboxShader->BindUniformBlock("Camera", 0);
        m_GridShader->BindUniformBlock("Camera", 0);
        m_OutlineShader->BindUniformBlock("Camera", 0);
        m_SpriteShader->BindUniformBlock("Camera", 0);

        m_DeferredMaterial = std::make_shared<Material>();
        m_DeferredMaterial->Name = "Deferred Lighting Material";

        // ==========================================
        // 创建目标 FBO
        // ==========================================
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
        // 固化各个子系统的管线 (PSO)
        // ==========================================
        // 1. 延迟光照全屏管线
        PipelineSpecification defSpec;
        defSpec.Layout = {};
        defSpec.Shader = m_DeferredLightingShader;
        defSpec.TargetFramebuffer = m_LightingFBO;
        defSpec.DepthTest = false;  
        defSpec.DepthWrite = false; 
        defSpec.Blend = false;
        defSpec.BackfaceCulling = CullMode::None;
        m_DeferredPipeline = Pipeline::Create(defSpec);

        // 2. 天空盒管线
        PipelineSpecification skySpec;
        skySpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        skySpec.Shader = m_SkyboxShader;
        skySpec.TargetFramebuffer = m_LightingFBO;
        skySpec.DepthTest = true;
        skySpec.DepthWrite = false; 
        skySpec.DepthFuncLEqual = true; 
        skySpec.Blend = false;
        skySpec.BackfaceCulling = CullMode::None;
        m_SkyboxPipeline = Pipeline::Create(skySpec);

        // 3. 无穷网格管线
        PipelineSpecification gridSpec;
        gridSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        gridSpec.Shader = m_GridShader;
        gridSpec.TargetFramebuffer = m_LightingFBO;
        gridSpec.DepthTest = true;
        gridSpec.DepthWrite = true;
        gridSpec.Blend = true;
        gridSpec.BackfaceCulling = CullMode::None;
        m_GridPipeline = Pipeline::Create(gridSpec);

        // 4. 2D精灵图管线
        PipelineSpecification spriteSpec;
        spriteSpec.Layout = {};
        spriteSpec.Shader = m_SpriteShader;
        spriteSpec.TargetFramebuffer = m_LightingFBO;
        spriteSpec.DepthTest = true;
        spriteSpec.DepthWrite = true;
        spriteSpec.Blend = true;
        spriteSpec.BackfaceCulling = CullMode::None;
        m_SpritePipeline = Pipeline::Create(spriteSpec);

        // 5. 选中描边管线 (网格)
        PipelineSpecification selMeshSpec;
        selMeshSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        selMeshSpec.Shader = m_OutlineShader;
        selMeshSpec.TargetFramebuffer = m_SelectionFBO;
        selMeshSpec.DepthTest = true;
        selMeshSpec.DepthWrite = true;
        selMeshSpec.Blend = false;
        selMeshSpec.PolygonModeLine = true; 
        selMeshSpec.LineWidth = 3.0f;
        selMeshSpec.BackfaceCulling = CullMode::None;
        m_SelectionMeshPipeline = Pipeline::Create(selMeshSpec);

        // 6. 选中描边管线 (2D Sprite)
        PipelineSpecification selSpriteSpec;
        selSpriteSpec.Layout = {};
        selSpriteSpec.Shader = m_SpriteShader;
        selSpriteSpec.TargetFramebuffer = m_SelectionFBO;
        selSpriteSpec.DepthTest = true;
        selSpriteSpec.DepthWrite = true;
        selSpriteSpec.Blend = true;
        selSpriteSpec.PolygonModeLine = true;
        selSpriteSpec.LineWidth = 3.0f;
        selSpriteSpec.BackfaceCulling = CullMode::None;
        m_SelectionSpritePipeline = Pipeline::Create(selSpriteSpec);

        s_SkyboxMesh = Mesh::CreateCube();
    }

    void VulkanLightingPass::OnResize(uint32_t width, uint32_t height) {
        m_LightingFBO->Resize(width, height);
        m_SelectionFBO->Resize(width, height);
    }

    void VulkanLightingPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");

        if (width == 0 || height == 0) return;

        if (m_LightingFBO->GetSpecification().Width != width || m_LightingFBO->GetSpecification().Height != height) {
            OnResize(width, height);
        }

        // ==========================================
        // 获取 G-Buffer 和 ShadowMap 依赖
        // ==========================================
        auto gbufferFBO = context.Get<std::shared_ptr<Framebuffer>>("GBuffer_FBO", nullptr);
        if (!gbufferFBO) return;

        auto shadowFBO = context.Get<std::shared_ptr<Framebuffer>>("ShadowMap_Output", nullptr);
        glm::mat4 lightSpaceMatrix = context.Get<glm::mat4>("LightSpaceMatrix", glm::mat4(1.0f));

        // 【核心安全】：请求 GPU 等待 GBuffer 和 ShadowMap 写完！
        cmd.InsertExecutionBarrier();

        float physicalExposure = context.Get<float>("PhysicalExposure", 1.0f);
        glm::vec4 hdrClearColor = glm::vec4(context.Get<glm::vec3>("AmbientColor", glm::vec3(0.06f)), 1.0f);

        // ==========================================
        // 渲染阶段 1：场景主画面 (Lighting + Forward)
        // ==========================================
        cmd.BeginRenderPass(m_LightingFBO, true, hdrClearColor);

        // 1. 延迟光照 (Deferred Lighting)
        cmd.BindPipeline(m_DeferredPipeline);
        context.Stats.ShaderBinds++;

        // 绑定 G-Buffer
        cmd.BindTexture2D(m_DeferredPipeline, "g_Position", 0, gbufferFBO, 0);
        cmd.BindTexture2D(m_DeferredPipeline, "g_Normal", 1, gbufferFBO, 1);
        cmd.BindTexture2D(m_DeferredPipeline, "g_Albedo", 2, gbufferFBO, 2);
        cmd.BindTexture2D(m_DeferredPipeline, "g_PBR", 3, gbufferFBO, 3);
        cmd.BindTexture2D(m_DeferredPipeline, "g_CustomData", 4, gbufferFBO, 4);

        // ==========================================
        // 【核心修复 2】：疯狂填坑！不允许任何槽位为空！
        // ==========================================
        auto whiteTex = context.GetTexture("WhiteTexture");
        if (whiteTex) {
            cmd.BindTexture2D(m_DeferredPipeline, "u_ShadowMap", 5, whiteTex);
            cmd.BindTexture2D(m_DeferredPipeline, "u_BRDFLUT", 10, whiteTex);
        }

        auto irrMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap", nullptr);
        auto preMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap", nullptr);
        
        // ==========================================
        // 【核心修复 2】：严格控制发车！
        // ==========================================
        if (irrMap && preMap) {
            cmd.BindTextureCube(m_DeferredPipeline, "u_IrradianceMap", 8, irrMap);
            cmd.BindTextureCube(m_DeferredPipeline, "u_PrefilteredMap", 9, preMap);
            
            // 只有绑好了贴图，才允许发车！
            cmd.DrawTriangleStrip(4); 
        } 
        else {
            AYAYA_CORE_WARN("No Skybox found! Skipping Lighting Pass to prevent Vulkan Crash.");
            // ⚠️ 绝对不能在这里或者在这段 if 之后执行 cmd.DrawTriangleStrip(4); ！！！
        }

        bool hasEnvMap = (irrMap && preMap);

        // 【重构】：组装并推送常量
        DeferredLightingPushConstants defConstants{};
        defConstants.LightSpaceMatrix = lightSpaceMatrix;
        defConstants.AmbientColor = context.Get<glm::vec3>("AmbientColor", glm::vec3(0.06f));
        defConstants.Intensity = context.Get<float>("EnvironmentIntensity", 1.0f);
        defConstants.EnvMapEnabled = hasEnvMap ? 1 : 0;
        cmd.PushConstantData(m_DeferredPipeline, &defConstants, sizeof(DeferredLightingPushConstants));

        if (context.RecordAndCheckDrawCall("Lighting Pass", "Deferred Shading", "Deferred Shader", 1)) {
            cmd.DrawArrays(3); // 画一个大三角形覆盖全屏
        }

        // 2. 拷贝深度，为正向渲染准备
        cmd.BlitDepth(gbufferFBO, m_LightingFBO, width, height);

        // 3. 正向渲染：天空盒
        auto envMap = context.Get<std::shared_ptr<TextureCube>>("EnvironmentMap", nullptr);
        if (envMap && context.Get<bool>("ShowSkybox", true)) {
            cmd.BindPipeline(m_SkyboxPipeline);
            context.Stats.ShaderBinds++;

            cmd.BindTextureCube(m_SkyboxPipeline, "u_Skybox", 0, envMap);

            SkyboxPushConstants skyConstants{};
            skyConstants.Projection = context.Get<glm::mat4>("CameraProjection", glm::mat4(1.0f));
            skyConstants.View = context.Get<glm::mat4>("CameraView", glm::mat4(1.0f));
            skyConstants.Transform = glm::mat4(1.0f);
            skyConstants.Intensity = context.Get<float>("EnvironmentIntensity", 1.0f);
            cmd.PushConstantData(m_SkyboxPipeline, &skyConstants, sizeof(SkyboxPushConstants));

            if (context.RecordAndCheckDrawCall("Lighting Pass", "Skybox", "Skybox Shader", s_SkyboxMesh->GetIndexCount() / 3)) {
                cmd.DrawIndexed(s_SkyboxMesh, s_SkyboxMesh->GetIndexCount());
            }
        }

        // 4. 正向渲染：无穷网格
        if (context.Get<bool>("ShowGrid", true)) {
            cmd.BindPipeline(m_GridPipeline);
            context.Stats.ShaderBinds++;

            GridPushConstants gridConstants{};
            gridConstants.Transform = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(100.0f));
            gridConstants.ExposureInverse = 1.0f / physicalExposure;
            cmd.PushConstantData(m_GridPipeline, &gridConstants, sizeof(GridPushConstants));

            if (context.RecordAndCheckDrawCall("Lighting Pass", "Editor Grid", "Grid Shader", 2)) {
                cmd.DrawArrays(6);
            }
        }

        // 5. 准备实体绘制列表
        m_SpriteDrawList.clear();
        m_SelectionMeshDrawList.clear();
        m_SelectionSpriteDrawList.clear();
        
        Entity selectedEntity = context.Get<Entity>("SelectedEntity", Entity{});
        glm::vec3 cameraPos = context.Get<glm::vec3>("CameraPosition", glm::vec3(0.0f));

        auto spriteView = context.ActiveScene->Reg().view<TransformComponent, SpriteRendererComponent>();
        for (auto entityID : spriteView) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;

            auto& spriteComp = entity.GetComponent<SpriteRendererComponent>();
            glm::mat4 transform = entity.GetWorldTransform();
            
            SpriteDrawCommand cmdData;
            cmdData.Transform = transform;
            cmdData.SpriteComp = spriteComp;
            cmdData.DistanceToCamera = glm::distance2(glm::vec3(transform[3]), cameraPos);
            
            m_SpriteDrawList.push_back(cmdData);

            if (entity == selectedEntity) {
                m_SelectionSpriteDrawList.push_back(cmdData);
            }
        }

        if (selectedEntity && selectedEntity.HasComponent<MeshRendererComponent>()) {
            auto& meshComp = selectedEntity.GetComponent<MeshRendererComponent>();
            if (meshComp.ModelAsset) {
                for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                    OutlineMeshCommand cmdData;
                    cmdData.Transform = selectedEntity.GetWorldTransform();
                    cmdData.MeshAsset = mesh;
                    m_SelectionMeshDrawList.push_back(cmdData);
                }
            }
        }

        std::sort(m_SpriteDrawList.begin(), m_SpriteDrawList.end(), [](const SpriteDrawCommand& a, const SpriteDrawCommand& b) {
            return a.DistanceToCamera > b.DistanceToCamera;
        });

        // 6. 正向渲染：2D 精灵
        if (!m_SpriteDrawList.empty()) {
            cmd.BindPipeline(m_SpritePipeline);
            context.Stats.ShaderBinds++;

            for (const auto& drawCmd : m_SpriteDrawList) {
                const auto& spriteComp = drawCmd.SpriteComp;
                
                SpritePushConstants spriteConstants{};
                spriteConstants.Transform = drawCmd.Transform;
                spriteConstants.Color = spriteComp.Color;
                spriteConstants.ExposureInverse = 1.0f / physicalExposure;

                if (spriteComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(spriteComp.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(spriteComp.TextureHandle);
                    cmd.BindTexture2D(m_SpritePipeline, "u_Texture", 0, tex);
                    spriteConstants.UseTexture = 1;
                } else {
                    auto whiteTex = context.GetTexture("WhiteTexture");
                    cmd.BindTexture2D(m_SpritePipeline, "u_Texture", 0, whiteTex);
                    spriteConstants.UseTexture = 0;
                }

                cmd.PushConstantData(m_SpritePipeline, &spriteConstants, sizeof(SpritePushConstants));

                if (context.RecordAndCheckDrawCall("Lighting Pass", "Sprite", "Sprite Shader", 2)) {
                    cmd.DrawTriangleStrip(4);
                }
            }
        }

        cmd.EndRenderPass();

        // ==========================================
        // 渲染阶段 2：选中物体描边轮廓 (Selection Outline)
        // ==========================================
        cmd.BeginRenderPass(m_SelectionFBO, true, glm::vec4(0.0f));

        if (!m_SelectionMeshDrawList.empty()) {
            cmd.BindPipeline(m_SelectionMeshPipeline);
            context.Stats.ShaderBinds++;

            for (const auto& drawCmd : m_SelectionMeshDrawList) {
                // AABB 微微放大的黑魔法
                glm::mat4 transform = drawCmd.Transform;
                auto aabb = drawCmd.MeshAsset->GetAABB();
                glm::vec3 extents = aabb.Max - aabb.Min;
                glm::vec3 center = (aabb.Max + aabb.Min) * 0.5f;
                float scaleFactor = 1.05f;
                glm::mat4 offsetMat = glm::translate(glm::mat4(1.0f), center);
                glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
                glm::mat4 negOffsetMat = glm::translate(glm::mat4(1.0f), -center);
                glm::mat4 outlineTransform = transform * offsetMat * scaleMat * negOffsetMat;

                OutlinePushConstants outConstants{};
                outConstants.Transform = outlineTransform;
                outConstants.Color = glm::vec3(1.0f, 0.5f, 0.0f); // 描边颜色
                cmd.PushConstantData(m_SelectionMeshPipeline, &outConstants, sizeof(OutlinePushConstants));

                if (context.RecordAndCheckDrawCall("Lighting Pass", "Selection Mesh", "Outline Shader", drawCmd.MeshAsset->GetIndexCount() / 3)) {
                    cmd.DrawIndexed(drawCmd.MeshAsset, drawCmd.MeshAsset->GetIndexCount());
                }
            }
        }

        if (!m_SelectionSpriteDrawList.empty()) {
            cmd.BindPipeline(m_SelectionSpritePipeline);
            context.Stats.ShaderBinds++;

            for (const auto& drawCmd : m_SelectionSpriteDrawList) {
                const auto& spriteComp = drawCmd.SpriteComp;
                
                // 给 2D Sprite 也微微放大一圈作为描边
                glm::mat4 transform = drawCmd.Transform;
                transform = glm::scale(transform, glm::vec3(1.05f, 1.05f, 1.0f));

                SpritePushConstants spriteConstants{};
                spriteConstants.Transform = transform;
                spriteConstants.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                spriteConstants.ExposureInverse = 1.0f;

                if (spriteComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(spriteComp.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(spriteComp.TextureHandle);
                    cmd.BindTexture2D(m_SelectionSpritePipeline, "u_Texture", 0, tex);
                    spriteConstants.UseTexture = 1;
                } else {
                    auto whiteTex = context.GetTexture("WhiteTexture");
                    cmd.BindTexture2D(m_SelectionSpritePipeline, "u_Texture", 0, whiteTex);
                    spriteConstants.UseTexture = 0;
                }

                cmd.PushConstantData(m_SelectionSpritePipeline, &spriteConstants, sizeof(SpritePushConstants));
                cmd.DrawTriangleStrip(4);
            }
        }
        
        cmd.EndRenderPass();

        // ==========================================
        // 产出数据交接
        // ==========================================
        context.Set("Lighting_Output", m_LightingFBO);
        context.Framebuffers["Lighting"] = m_LightingFBO;
        
        context.Set("Selection_Output", m_SelectionFBO);
        context.Framebuffers["Selection"] = m_SelectionFBO;

        context.Set("Final_Output", m_LightingFBO);
    }

}