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

        FramebufferSpecification fboSpec;
        fboSpec.Samples = 1;
        fboSpec.Width = 1280;
        fboSpec.Height = 720;
        fboSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };

        m_ForwardFBOs.resize(3);
        for (int i = 0; i < 3; i++) m_ForwardFBOs[i] = Framebuffer::Create(fboSpec);

        PipelineSpecification pipeSpec;
        pipeSpec.Shader = m_ForwardShader;
        pipeSpec.TargetFramebuffer = m_ForwardFBOs[0];
        pipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        pipeSpec.DepthTest = true;
        pipeSpec.DepthWrite = true;
        pipeSpec.Blend = false;
        pipeSpec.BackfaceCulling = CullMode::None;
        m_ForwardPipeline = Pipeline::Create(pipeSpec);

        m_SkyboxShader = Shader::Create("Skybox/skybox.vert", "Skybox/skybox.frag");
        PipelineSpecification skyboxPipeSpec;
        skyboxPipeSpec.Shader = m_SkyboxShader;
        skyboxPipeSpec.TargetFramebuffer = m_ForwardFBOs[0];
        skyboxPipeSpec.Layout = pipeSpec.Layout;
        skyboxPipeSpec.DepthTest = true;
        skyboxPipeSpec.DepthWrite = false;
        skyboxPipeSpec.DepthOperator = DepthCompareOperator::LEqual;
        skyboxPipeSpec.BackfaceCulling = CullMode::None;
        m_SkyboxPipeline = Pipeline::Create(skyboxPipeSpec);

        // Selection FBO (选中描边)
        FramebufferSpecification selSpec;
        selSpec.Samples = 1;
        selSpec.Width = 1280; selSpec.Height = 720;
        selSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_SelectionFBO = Framebuffer::Create(selSpec);

        m_OutlineShader = Shader::Create("UI/outline.vert", "UI/outline.frag");
        PipelineSpecification outlineSpec;
        outlineSpec.Shader = m_OutlineShader;
        outlineSpec.TargetFramebuffer = m_SelectionFBO;
        outlineSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        outlineSpec.DepthTest = false;
        outlineSpec.DepthWrite = true;
        outlineSpec.Blend = false;
        outlineSpec.BackfaceCulling = CullMode::None;
        m_OutlinePipeline = Pipeline::Create(outlineSpec);
    }

    void VulkanForwardTestPass::OnResize(uint32_t width, uint32_t height) {
        for (auto& fbo : m_ForwardFBOs) fbo->Resize(width, height);
    }

    void VulkanForwardTestPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");
        if (width == 0 || height == 0) return;

        auto vulkanContext = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        uint32_t frameIndex = vulkanContext->GetCurrentFrameIndex() % m_ForwardFBOs.size();
        auto currentFBO = m_ForwardFBOs[frameIndex];
        if (currentFBO->GetSpecification().Width != width || currentFBO->GetSpecification().Height != height) {
            OnResize(width, height);
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

        cmd.BeginRenderPass(currentFBO, true, glm::vec4(0.12f, 0.12f, 0.14f, 1.0f));

        std::shared_ptr<Pipeline> currentPipeline = nullptr;

        // 【提取安全占位图】：提取黑板上 100% 绝对安全的白图
        auto whiteTex = context.GetTexture("WhiteTexture");

        auto envCubemap    = context.Get<std::shared_ptr<TextureCube>>("EnvironmentCubemap");
        auto irradianceMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap");
        auto prefilterMap  = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
        auto brdfLUT       = context.GetTexture("BRDFLUT");

        if (!envCubemap) {
            cmd.EndRenderPass();
            // 即使 envCubemap 为空，也必须注册 Forward FBO，否则 PostProcess 会拿到上一帧的僵尸画布
            context.Set("Forward_Output", currentFBO);
            context.Set("Final_Output", currentFBO);
            context.Framebuffers["ForwardTest"] = currentFBO;
            return;
        }

        // 环境参数 — 对齐 OpenGL 的 u_Intensity / u_AmbientColor
        float envIntensity = context.Get<float>("EnvironmentIntensity", 1.0f);
        glm::vec3 envAmbient = context.Get<glm::vec3>("EnvironmentAmbientColor", glm::vec3(0.1f))
                             * envIntensity;

        // 【安全降级策略】
        std::shared_ptr<TextureCube> safeIrradiance = irradianceMap ? irradianceMap : envCubemap;
        std::shared_ptr<TextureCube> safePrefilter  = prefilterMap  ? prefilterMap  : envCubemap;
        std::shared_ptr<Texture2D>   safeBRDF       = brdfLUT       ? brdfLUT       : whiteTex;

        for (const auto& drawCmd : m_OpaqueDrawList) {
            if (currentPipeline != drawCmd.PipelineAsset) {
                currentPipeline = drawCmd.PipelineAsset;
                cmd.BindPipeline(currentPipeline);
                context.Stats.ShaderBinds++;
            }

            // ==========================================
            // 【硬核安全发车规则】：每次画物体，强行填满所有槽位！
            // ==========================================
            cmd.BindTexture2D(currentPipeline,   "u_AlbedoMap",     0, whiteTex);
            cmd.BindTextureCube(currentPipeline, "u_IrradianceMap", 1, safeIrradiance);
            cmd.BindTextureCube(currentPipeline, "u_PrefilterMap",  2, safePrefilter);
            cmd.BindTexture2D(currentPipeline,   "u_BRDFLUT",       3, safeBRDF);
            cmd.BindTexture2D(currentPipeline,   "u_MetallicMap",   4, whiteTex);
            cmd.BindTexture2D(currentPipeline,   "u_RoughnessMap",  5, whiteTex);
            cmd.BindTexture2D(currentPipeline,   "u_AOMap",         6, whiteTex);
            cmd.BindTexture2D(currentPipeline,   "u_NormalMap",     7, whiteTex);

            ForwardPushConstants constants{};
            constants.Transform = drawCmd.Transform;
            constants.Albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            constants.UseAlbedoMap = 0;
            constants.Metallic = 0.0f;
            constants.Roughness = 0.5f;
            constants.AO = 1.0f;
            constants.UseMetallicMap = 0;
            constants.UseRoughnessMap = 0;
            constants.UseAOMap = 0;
            constants.UseNormalMap = 0;
            constants.EnvironmentIntensity = envIntensity;
            constants._pad0 = 0.0f;
            constants._pad1 = 0.0f;
            constants._pad2 = 0.0f;
            constants.EnvironmentAmbientColor = glm::vec4(envAmbient, 1.0f);

            if (drawCmd.MaterialAsset) {
                for (const auto& prop : drawCmd.MaterialAsset->Properties) {
                    if (prop.Type == MaterialPropertyType::Vec3 && prop.UniformName == "u_Albedo") {
                        constants.Albedo = glm::vec4(prop.Vec3Value, 1.0f);
                    }
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Metallic") {
                        constants.Metallic = prop.FloatValue;
                    }
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Roughness") {
                        constants.Roughness = prop.FloatValue;
                    }
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_AO") {
                        constants.AO = prop.FloatValue;
                    }
                    else if (prop.Type == MaterialPropertyType::Texture2D) {
                        bool hasValidTex = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) || (prop.RuntimeTexture != nullptr);
                        if (!hasValidTex) continue;
                        auto tex = prop.RuntimeTexture ? prop.RuntimeTexture : AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                        if (!tex) continue;

                        if (prop.UniformName == "u_AlbedoMap") {
                            cmd.BindTexture2D(currentPipeline, "u_AlbedoMap", 0, tex);
                            constants.UseAlbedoMap = 1;
                        } else if (prop.UniformName == "u_MetallicMap") {
                            cmd.BindTexture2D(currentPipeline, "u_MetallicMap", 4, tex);
                            constants.UseMetallicMap = 1;
                        } else if (prop.UniformName == "u_RoughnessMap") {
                            cmd.BindTexture2D(currentPipeline, "u_RoughnessMap", 5, tex);
                            constants.UseRoughnessMap = 1;
                        } else if (prop.UniformName == "u_AOMap") {
                            cmd.BindTexture2D(currentPipeline, "u_AOMap", 6, tex);
                            constants.UseAOMap = 1;
                        } else if (prop.UniformName == "u_NormalMap") {
                            cmd.BindTexture2D(currentPipeline, "u_NormalMap", 7, tex);
                            constants.UseNormalMap = 1;
                        }
                    }
                }
            }

            cmd.PushConstantData(currentPipeline, &constants, sizeof(ForwardPushConstants));

            std::string tag = drawCmd.TargetEntity.GetComponent<TagComponent>().Tag;
            uint32_t tris = drawCmd.MeshAsset->GetIndexCount() / 3;
            if (context.RecordAndCheckDrawCall("Forward Test Pass", tag, "Forward Shader", tris)) {
                cmd.DrawIndexed(drawCmd.MeshAsset, drawCmd.MeshAsset->GetIndexCount());
            }
        }

        bool showSkybox = context.Get<bool>("ShowSkybox");
        auto skyboxMesh = context.Get<std::shared_ptr<Mesh>>("SkyboxMesh");

        if (showSkybox && skyboxMesh && envCubemap) {
            cmd.BindPipeline(m_SkyboxPipeline);

            SkyboxPushConstants skyboxConstants;
            glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(context.ViewMatrix));
            skyboxConstants.ViewProjection = context.ProjectionMatrix * viewNoTranslation;
            skyboxConstants.Intensity = context.Get<float>("EnvironmentIntensity", 1.0f);

            cmd.PushConstantData(m_SkyboxPipeline, &skyboxConstants, sizeof(SkyboxPushConstants));
            cmd.BindTextureCube(m_SkyboxPipeline, "u_Skybox", 0, envCubemap);

            if (context.RecordAndCheckDrawCall("Forward Pass", "Skybox", "Skybox Shader", skyboxMesh->GetIndexCount() / 3)) {
                cmd.DrawIndexed(skyboxMesh, skyboxMesh->GetIndexCount());
            }
        }

        cmd.EndRenderPass();

        // Selection FBO — 始终写入，确保取消选中时清除旧数据
        if (m_SelectionFBO->GetSpecification().Width != width || m_SelectionFBO->GetSpecification().Height != height) {
            m_SelectionFBO->Resize(width, height);
        }
        cmd.BeginRenderPass(m_SelectionFBO, true, glm::vec4(0.0f));

        Entity hoveredEntity = context.Get<Entity>("HoveredEntity", Entity{});
        if (hoveredEntity && hoveredEntity.HasComponent<MeshRendererComponent>()) {
            auto& meshComp = hoveredEntity.GetComponent<MeshRendererComponent>();
            auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
            if (model) {
                cmd.BindPipeline(m_OutlinePipeline);
                context.Stats.ShaderBinds++;
                glm::mat4 transform = hoveredEntity.GetWorldTransform();
                for (auto& mesh : model->GetMeshes()) {
                    auto aabb = mesh->GetAABB();
                    glm::vec3 center = (aabb.Max + aabb.Min) * 0.5f;
                    float scaleFactor = 1.05f;
                    glm::mat4 offsetMat = glm::translate(glm::mat4(1.0f), center);
                    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
                    glm::mat4 negOffsetMat = glm::translate(glm::mat4(1.0f), -center);
                    glm::mat4 outlineTransform = transform * offsetMat * scaleMat * negOffsetMat;

                    struct { glm::mat4 Transform; alignas(16) glm::vec3 Color; } outlinePC;
                    outlinePC.Transform = outlineTransform;
                    outlinePC.Color = glm::vec3(1.0f, 0.5f, 0.0f);
                    cmd.PushConstantData(m_OutlinePipeline, &outlinePC, sizeof(outlinePC));

                    cmd.DrawIndexed(mesh, mesh->GetIndexCount());
                }
            }
        }
        cmd.EndRenderPass();
        context.Framebuffers["Selection"] = m_SelectionFBO;

        cmd.InsertExecutionBarrier(); // flush TBDR tile writes before PostProcess reads this FBO
        context.Set("Forward_Output", currentFBO);
        context.Set("Final_Output", currentFBO);
        context.Framebuffers["ForwardTest"] = currentFBO;
    }
}
