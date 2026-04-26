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
        // 必须使用 RGBA16F，否则高光会被强制砍成 1.0 导致纯白！
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
            if (!meshComp.ModelAsset) continue;

            glm::mat4 transform = entity.GetWorldTransform();
            bool isVisible = false;
            for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                if (cameraFrustum.IsBoxVisible(mesh->GetAABB(), transform)) { isVisible = true; break; }
            }
            if (!isVisible) continue;

            auto targetMaterial = meshComp.MaterialAsset ? meshComp.MaterialAsset : m_DefaultMaterial;

            for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
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
        auto whiteTex = context.GetTexture("WhiteTexture");

        auto envCubemap    = context.Get<std::shared_ptr<TextureCube>>("EnvironmentCubemap");
        auto irradianceMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap");
        auto prefilterMap  = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
        
        // 【核心修复 1】：必须用 GetTexture 获取 BRDFLUT
        auto brdfLUT       = context.GetTexture("BRDFLUT");

        if (!envCubemap) {
            cmd.EndRenderPass();
            return;
        }

        // ==========================================
        // 【终极防线】：在内存中硬核创建一个 1x1 白图
        // 彻底杜绝 Vulkan 拿到 nullptr 导致整个描述符刷新崩溃！
        // ==========================================
        static std::shared_ptr<Texture2D> s_FallbackWhiteTex = nullptr;
        if (!s_FallbackWhiteTex) {
            s_FallbackWhiteTex = Texture2D::Create(1, 1);
            uint32_t whiteData = 0xffffffff;
            s_FallbackWhiteTex->SetData(&whiteData, sizeof(uint32_t));
        }

        // 智能兜底：就算黑板上没有，也能用白图或天空盒顶上，保证 Vulkan 安检 100% 通过
        std::shared_ptr<TextureCube> safeIrradiance = irradianceMap ? irradianceMap : envCubemap;
        std::shared_ptr<TextureCube> safePrefilter  = prefilterMap  ? prefilterMap  : envCubemap;
        std::shared_ptr<Texture2D>   safeBRDF       = brdfLUT       ? brdfLUT       : s_FallbackWhiteTex;

        for (const auto& drawCmd : m_OpaqueDrawList) {
            if (currentPipeline != drawCmd.PipelineAsset) {
                currentPipeline = drawCmd.PipelineAsset;
                cmd.BindPipeline(currentPipeline);
                context.Stats.ShaderBinds++;
            }

            // ==========================================
            // 【核心修复 2】：无条件绑定 IBL，彻底斩杀 08114 报错
            // ==========================================
            cmd.BindTextureCube(currentPipeline, "u_IrradianceMap", 1, safeIrradiance);
            cmd.BindTextureCube(currentPipeline, "u_PrefilterMap",  2, safePrefilter);
            cmd.BindTexture2D(currentPipeline,   "u_BRDFLUT",       3, safeBRDF);

            ForwardPushConstants constants{};
            constants.Transform = drawCmd.Transform;
            constants.Albedo = glm::vec3(1.0f);
            constants.UseAlbedoMap = 0;

            // 【修改】：使用我们自己创建的绝对安全白图兜底
            if (s_FallbackWhiteTex) cmd.BindTexture2D(currentPipeline, "u_AlbedoMap", 0, s_FallbackWhiteTex);


            // --- 给着色器注入一个经典的左上方阳光 ---
            constants.LightDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
            constants.LightColor = glm::vec3(5.0f); // 5.0 的光强，在 HDR 下会非常亮眼

            // 材质默认参数
            constants.Metallic = 0.0f;
            constants.Roughness = 0.4f;

            if (drawCmd.MaterialAsset) {
                for (const auto& prop : drawCmd.MaterialAsset->Properties) {
                    if (prop.Type == MaterialPropertyType::Vec3 && prop.UniformName == "u_Albedo") {
                        constants.Albedo = prop.Vec3Value;
                    } 
                    else if (prop.Type == MaterialPropertyType::Texture2D && prop.UniformName == "u_AlbedoMap") {
                        bool hasValidTex = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) || (prop.RuntimeTexture != nullptr);
                        if (hasValidTex) {
                            auto tex = prop.RuntimeTexture ? prop.RuntimeTexture : AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                            cmd.BindTexture2D(currentPipeline, "u_AlbedoMap", 0, tex);
                            constants.UseAlbedoMap = 1;
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
        context.Set("Forward_Output", currentFBO); 
        context.Set("Final_Output", currentFBO);   
        context.Framebuffers["ForwardTest"] = currentFBO;
    }
}