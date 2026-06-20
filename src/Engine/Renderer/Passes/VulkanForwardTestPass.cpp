#include "ayapch.h"
#include "VulkanForwardTestPass.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Frustum.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Core/Application.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Ayaya {

    struct SkyboxPushConstants {
        glm::mat4 ViewProjection;
        float Intensity;
    };

    VulkanForwardTestPass::VulkanForwardTestPass() {
        m_PassName = "Vulkan Forward Test Pass";
    }

    void VulkanForwardTestPass::OnAttach() {
        m_ForwardShader = Shader::Create("Debug/pbr_forward.vert", "Debug/pbr_forward.frag");

        m_DefaultMaterial = std::make_shared<Material>();
        m_DefaultMaterial->Name = "Forward Default Material";

        m_ForwardPipeSpec.Shader = m_ForwardShader;
        m_ForwardPipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        m_ForwardPipeSpec.DepthTest = true;
        m_ForwardPipeSpec.DepthWrite = true;
        m_ForwardPipeSpec.Blend = false;
        m_ForwardPipeSpec.BackfaceCulling = CullMode::Back;

        m_SkyboxShader = Shader::Create("Skybox/skybox.vert", "Skybox/skybox.frag");
        m_SkyboxPipeSpec.Shader = m_SkyboxShader;
        m_SkyboxPipeSpec.Layout = m_ForwardPipeSpec.Layout;
        m_SkyboxPipeSpec.DepthTest = true;
        m_SkyboxPipeSpec.DepthWrite = false;
        m_SkyboxPipeSpec.DepthOperator = DepthCompareOperator::LEqual;
        m_SkyboxPipeSpec.BackfaceCulling = CullMode::None;

        m_SpriteShader = Shader::Create("2D/sprite.vert", "2D/sprite.frag");
        m_SpritePipeSpec.Shader = m_SpriteShader;
        m_SpritePipeSpec.Layout = {}; // shader 用 gl_VertexIndex 程序化生成顶点
        m_SpritePipeSpec.Topology = PrimitiveTopology::TriangleStrip;
        m_SpritePipeSpec.DepthTest = true;
        m_SpritePipeSpec.DepthWrite = false;
        m_SpritePipeSpec.Blend = true;
        m_SpritePipeSpec.BlendMode = BlendModeType::Alpha;
        m_SpritePipeSpec.BackfaceCulling = CullMode::None;
    }

    void VulkanForwardTestPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        FramebufferSpecification fboSpec;
        fboSpec.Width  = width;
        fboSpec.Height = height;
        fboSpec.Samples = 1;
        fboSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        builder.WriteTexture("SceneColor", fboSpec);
        builder.ReadTexture("ShadowMap");
    }

    void VulkanForwardTestPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");
        if (width == 0 || height == 0) return;

        // 从 Graph 管理的 Context 黑板获取 FBO (替代原来的 m_ForwardFBOs[m_FrameIndex])
        auto sceneColorFBO = context.GetFramebuffer("SceneColor");
        if (!sceneColorFBO) {
            AYAYA_CORE_ERROR("VulkanForwardTestPass: SceneColor FBO not found in context!");
            return;
        }

        if (!m_ForwardPipeline) {
            m_ForwardPipeSpec.TargetFramebuffer = sceneColorFBO;
            m_ForwardPipeline = Pipeline::Create(m_ForwardPipeSpec);
            m_SkyboxPipeSpec.TargetFramebuffer = sceneColorFBO;
            m_SkyboxPipeline = Pipeline::Create(m_SkyboxPipeSpec);
        }
        if (!m_SpritePipeline) {
            m_SpritePipeSpec.TargetFramebuffer = sceneColorFBO;
            m_SpritePipeline = Pipeline::Create(m_SpritePipeSpec);
        }

        m_OpaqueDrawList.clear();
        glm::mat4 viewProj = context.ProjectionMatrix * context.ViewMatrix;
        Frustum cameraFrustum(viewProj);

        auto meshGroup = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshGroup) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;

            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
            if (!model) continue;

            glm::mat4 transform = entity.GetWorldTransform();
            bool isVisible = false;
            for (auto& mesh : model->GetMeshes()) {
                if (cameraFrustum.IsBoxVisible(mesh->GetAABB(), transform)) { isVisible = true; break; }
            }
            if (!isVisible) continue;
            auto material = AssetManager::GetAsset<Material>(meshComp.MaterialHandle);
            auto targetMaterial = material ? material : m_DefaultMaterial;

            for (auto& mesh : model->GetMeshes()) {
                VulkanForwardCommandData drawCmd;
                drawCmd.Transform = transform;
                drawCmd.MeshAsset = mesh;
                drawCmd.MaterialAsset = targetMaterial;
                drawCmd.PipelineAsset = m_ForwardPipeline;
                drawCmd.TargetEntity = entity;
                m_OpaqueDrawList.push_back(drawCmd);
            }
        }

        std::sort(m_OpaqueDrawList.begin(), m_OpaqueDrawList.end(), [](const auto& a, const auto& b) {
            if (a.PipelineAsset.get() != b.PipelineAsset.get()) return a.PipelineAsset.get() < b.PipelineAsset.get();
            if (a.MaterialAsset.get() != b.MaterialAsset.get()) return a.MaterialAsset.get() < b.MaterialAsset.get();
            return a.MeshAsset.get() < b.MeshAsset.get();
        });

        cmd.BeginRenderPass(sceneColorFBO, true, glm::vec4(0.12f, 0.12f, 0.14f, 1.0f));

        std::shared_ptr<Pipeline> currentPipeline = nullptr;
        auto whiteTex = context.GetTexture("WhiteTexture");

        auto envCubemap    = context.Get<std::shared_ptr<TextureCube>>("EnvironmentCubemap");
        auto irradianceMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap");
        auto prefilterMap  = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
        auto brdfLUT       = context.GetTexture("BRDFLUT");

        float envIntensity = context.Get<float>("EnvironmentIntensity", 1.0f);
        glm::vec3 envAmbient = context.Get<glm::vec3>("EnvironmentAmbientColor", glm::vec3(0.1f)) * envIntensity;

        std::shared_ptr<TextureCube> safeIrradiance = irradianceMap ? irradianceMap : envCubemap;
        std::shared_ptr<TextureCube> safePrefilter  = prefilterMap  ? prefilterMap  : envCubemap;
        std::shared_ptr<Texture2D>   safeBRDF       = brdfLUT       ? brdfLUT       : whiteTex;

        auto shadowFBO = context.GetFramebuffer("ShadowMap");
        glm::mat4 lightSpaceMatrix = context.Get<glm::mat4>("LightSpaceMatrix", glm::mat4(1.0f));
        bool hasShadows = shadowFBO != nullptr;

        for (const auto& drawCmd : m_OpaqueDrawList) {
            if (currentPipeline != drawCmd.PipelineAsset) {
                currentPipeline = drawCmd.PipelineAsset;
                cmd.BindPipeline(currentPipeline);
                context.Stats.ShaderBinds++;
            }

            cmd.BindTexture2D(currentPipeline,   "u_AlbedoMap",     0, whiteTex);
            cmd.BindTextureCube(currentPipeline, "u_IrradianceMap", 1, safeIrradiance);
            cmd.BindTextureCube(currentPipeline, "u_PrefilterMap",  2, safePrefilter);
            cmd.BindTexture2D(currentPipeline,   "u_BRDFLUT",       3, safeBRDF);
            cmd.BindTexture2D(currentPipeline,   "u_MetallicMap",   4, whiteTex);
            cmd.BindTexture2D(currentPipeline,   "u_RoughnessMap",  5, whiteTex);
            cmd.BindTexture2D(currentPipeline,   "u_AOMap",         6, whiteTex);
            cmd.BindTexture2D(currentPipeline,   "u_NormalMap",     7, whiteTex);
            if (hasShadows)
                cmd.BindTexture2D(currentPipeline, "u_ShadowMap", 8, shadowFBO, 0, true);
            else
                cmd.BindTexture2D(currentPipeline, "u_ShadowMap", 8, whiteTex);

            bool debugRed = context.Get<bool>("DebugRed", false);

            ForwardPushConstants constants{};
            constants.Transform = drawCmd.Transform;
            constants.Albedo   = debugRed ? glm::vec4(1,0,0,1) : glm::vec4(1.0f);
            constants.Metallic = debugRed ? 0.0f : 0.0f;
            constants.Roughness= debugRed ? 0.5f : 0.5f;
            constants.AO       = debugRed ? 1.0f : 1.0f;
            constants.EnvironmentIntensity = debugRed ? 0.0f : envIntensity;
            constants.EnvironmentAmbientColor = debugRed ? glm::vec4(0,0,0,0) : glm::vec4(envAmbient, 1.0f);
            constants.LightSpaceMatrix = debugRed ? glm::mat4(1.0f) : lightSpaceMatrix;
            bool receiveShadows = hasShadows && drawCmd.TargetEntity.HasComponent<MeshRendererComponent>()
                && drawCmd.TargetEntity.GetComponent<MeshRendererComponent>().ReceiveShadows;
            constants.EnableShadows = (debugRed || !receiveShadows) ? 0 : 1;

            if (!debugRed && drawCmd.MaterialAsset) {
                for (const auto& prop : drawCmd.MaterialAsset->Properties) {
                    if (prop.Type == MaterialPropertyType::Vec3 && prop.UniformName == "u_Albedo")
                        constants.Albedo = glm::vec4(prop.Vec3Value, 1.0f);
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Metallic")
                        constants.Metallic = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Roughness")
                        constants.Roughness = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_AO")
                        constants.AO = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Texture2D) {
                        bool valid = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) || prop.RuntimeTexture;
                        if (!valid) continue;
                        auto tex = prop.RuntimeTexture ? prop.RuntimeTexture : AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                        if (!tex) continue;

                        if (prop.UniformName == "u_AlbedoMap")
                            { cmd.BindTexture2D(currentPipeline, "u_AlbedoMap", 0, tex); constants.UseAlbedoMap = 1; }
                        else if (prop.UniformName == "u_MetallicMap")
                            { cmd.BindTexture2D(currentPipeline, "u_MetallicMap", 4, tex); constants.UseMetallicMap = 1; }
                        else if (prop.UniformName == "u_RoughnessMap")
                            { cmd.BindTexture2D(currentPipeline, "u_RoughnessMap", 5, tex); constants.UseRoughnessMap = 1; }
                        else if (prop.UniformName == "u_AOMap")
                            { cmd.BindTexture2D(currentPipeline, "u_AOMap", 6, tex); constants.UseAOMap = 1; }
                        else if (prop.UniformName == "u_NormalMap")
                            { cmd.BindTexture2D(currentPipeline, "u_NormalMap", 7, tex); constants.UseNormalMap = 1; }
                    }
                }
            }

            cmd.PushConstantData(currentPipeline, &constants, sizeof(ForwardPushConstants));
            std::string tag = drawCmd.TargetEntity.GetComponent<TagComponent>().Tag;
            uint32_t tris = drawCmd.MeshAsset->GetIndexCount() / 3;
            if (context.RecordAndCheckDrawCall("Forward Test Pass", tag, "Forward Shader", tris))
                cmd.DrawIndexed(drawCmd.MeshAsset, drawCmd.MeshAsset->GetIndexCount());
        }

        // ==========================================
        // 2D Sprite 渲染 (Painter's Algorithm: 远→近)
        // ==========================================
        {
            struct SpriteDrawCmd {
                glm::mat4 Transform;
                SpriteRendererComponent SpriteComp;
                float Distance;
            };
            std::vector<SpriteDrawCmd> spriteList;
            auto spriteGroup = context.ActiveScene->Reg().view<TransformComponent, SpriteRendererComponent>();
            for (auto entityID : spriteGroup) {
                Entity entity{ entityID, context.ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue;
                auto [transformComp, spriteComp] = spriteGroup.get<TransformComponent, SpriteRendererComponent>(entityID);
                glm::mat4 transform = entity.GetWorldTransform();
                float dist = glm::length(context.CameraPosition - glm::vec3(transform[3]));
                spriteList.push_back({transform, spriteComp, dist});
            }
            std::sort(spriteList.begin(), spriteList.end(),
                [](const SpriteDrawCmd& a, const SpriteDrawCmd& b) { return a.Distance > b.Distance; });

            // AYAYA_CORE_WARN("[Sprite] raw={} active={} spritePipe={}",
            //     rawSpriteCount, (int)spriteList.size(), (void*)m_SpritePipeline.get());

            if (!spriteList.empty()) {
                cmd.BindPipeline(m_SpritePipeline);
                context.Stats.ShaderBinds++;
                for (auto& cmd2 : spriteList) {
                    // 字段顺序必须与 shader push_constant 对齐
                    struct { glm::mat4 Transform; glm::vec4 Color; float ExposureInv; int UseTexture; } spritePC;
                    spritePC.Transform = cmd2.Transform;
                    spritePC.Color = cmd2.SpriteComp.Color;
                    spritePC.ExposureInv = 1.0f / context.Get<float>("PhysicalExposure", 1.0f);

                    if (cmd2.SpriteComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(cmd2.SpriteComp.TextureHandle)) {
                        auto tex = AssetManager::GetAsset<Texture2D>(cmd2.SpriteComp.TextureHandle);
                        if (tex) { cmd.BindTexture2D(m_SpritePipeline, "u_Texture", 0, tex); spritePC.UseTexture = 1; }
                        else     { cmd.BindTexture2D(m_SpritePipeline, "u_Texture", 0, whiteTex); spritePC.UseTexture = 0; }
                    } else {
                        cmd.BindTexture2D(m_SpritePipeline, "u_Texture", 0, whiteTex);
                        spritePC.UseTexture = 0;
                    }
                    cmd.PushConstantData(m_SpritePipeline, &spritePC, sizeof(spritePC));
                    if (context.RecordAndCheckDrawCall("Forward Pass", "Sprite", "Sprite Shader", 2))
                        cmd.DrawTriangleStrip(4);
                }
            }
        }

        bool showSkybox = context.Get<bool>("ShowSkybox");
        auto skyboxMesh = context.Get<std::shared_ptr<Mesh>>("SkyboxMesh");
        if (showSkybox && skyboxMesh && envCubemap) {
            cmd.BindPipeline(m_SkyboxPipeline);
            SkyboxPushConstants skyConst;
            glm::mat4 skyProj = context.ProjectionMatrix;
            if (skyProj[3][3] == 1.0f) {
                uint32_t vpW = context.Get<uint32_t>("ViewportWidth", 1920);
                uint32_t vpH = context.Get<uint32_t>("ViewportHeight", 1080);
                float aspect = (float)vpW / (float)vpH;
                skyProj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
                skyProj[1][1] *= -1.0f; // Vulkan Y-flip
            }
            skyConst.ViewProjection = skyProj * glm::mat4(glm::mat3(context.ViewMatrix));
            skyConst.Intensity = context.Get<float>("EnvironmentIntensity", 1.0f);
            cmd.PushConstantData(m_SkyboxPipeline, &skyConst, sizeof(SkyboxPushConstants));
            cmd.BindTextureCube(m_SkyboxPipeline, "u_Skybox", 0, envCubemap);
            if (context.RecordAndCheckDrawCall("Forward Pass", "Skybox", "Skybox Shader", skyboxMesh->GetIndexCount() / 3))
                cmd.DrawIndexed(skyboxMesh, skyboxMesh->GetIndexCount());
        }

        cmd.EndRenderPass();
        context.Framebuffers["ForwardTest"] = sceneColorFBO;
    }

}
