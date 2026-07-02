#include "ayapch.h"
#include <any>
#include <chrono>

// 1. 核心系统
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Components.hpp"

// 2. 渲染管线与引擎基础设施
#include "Renderer/SceneRenderer.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/TextureCube.hpp"
#include "Renderer/Frustum.hpp"
#include "Renderer/IBLBuilder.hpp"
#include "Asset/AssetManager.hpp"

// 管线 Passes
#include "Renderer/Passes/ShadowPass.hpp"
#include "Renderer/Passes/UIPass.hpp"
#include "Renderer/Passes/GBufferPass.hpp"
#include "Renderer/Passes/LightingPass.hpp"
#include "Renderer/Passes/PostProcessPass.hpp"
#include "Renderer/Passes/BloomPass.hpp"
#include "Renderer/Passes/FXAAPass.hpp"
#include "Renderer/Passes/VulkanClearPass.hpp"
#include "Renderer/Passes/VulkanGbufferPass.hpp"
#include "Renderer/Passes/VulkanDepthPrePass.hpp"
#include "Renderer/Passes/VulkanLightingPass.hpp"
#include "Renderer/Passes/VulkanPostProcessPass.hpp"
#include "Renderer/Passes/VulkanForwardTestPass.hpp"
#include "Renderer/Passes/VulkanForwardBlendPass.hpp"
#include "Renderer/Passes/VulkanOutlinePass.hpp"
#include "Renderer/Passes/VulkanOutlinePass.hpp"
#include "Renderer/Passes/VulkanShadowPass.hpp"
#include "Renderer/Passes/VulkanBloomPass.hpp"
#include "Renderer/Passes/VulkanFXAAPass.hpp"
#include "Renderer/Passes/VulkanSSAOPass.hpp"
#include "Renderer/Passes/VulkanWBOITPass.hpp"
#include "Renderer/GDRContext.hpp"
#include "Renderer/PassRegistry.hpp"
#include "Renderer/PipelineBuilder.hpp"
#include "Renderer/RenderQueue.hpp"
#include "Renderer/Frustum.hpp"

#include "Core/Application.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanIBLBuilder.hpp"
#include "Platform/Vulkan/VulkanTextureCube.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanUniformBuffer.hpp"

// 3. 第三方库
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Ayaya {
    struct struct_CameraData {
        glm::mat4 ViewProjection;   // 64   (offset 0)
        glm::mat4 View;             // 64   (offset 64)
        glm::vec3 CameraPosition;   // 12   (offset 128)
        float     _padding0;        //  4   (offset 140)
        glm::vec4 ScreenParams;     // 16   (offset 144) — x=w, y=h, z=1+1/w, w=1+1/h
        glm::vec4 Time;             // 16   (offset 160) — x=t/20, y=t, z=t*2, w=t*3
    }; // 176 bytes

    // Directional light only (small UBO, Set 0 Binding 2)
    struct struct_DirLight {
        glm::vec4 DirLightDir;
        glm::vec4 DirLightColor;
    };

    // Point lights — SSBO (Set 0 Binding 1), unlimited count
    struct GPUPointLight {
        glm::vec4 positionAndRadius;  // xyz=world pos, w=radius
        glm::vec4 colorAndFalloff;    // rgb=radiance, w=falloff
    };

    // static std::shared_ptr<UniformBuffer> s_CameraUniformBuffer;
    // static std::shared_ptr<UniformBuffer> s_LightUniformBuffer;

    static std::shared_ptr<Mesh> s_SkyboxMesh;
    static std::shared_ptr<Shader> s_SkyboxShader;

    static std::shared_ptr<TextureCube> s_DefaultEnvironmentMap;
    static std::shared_ptr<TextureCube> s_DefaultIrradianceMap;
    static std::shared_ptr<TextureCube> s_DefaultPrefilterMap;
    static std::shared_ptr<Texture2D>   s_DefaultBRDFLUT;

    struct SceneRendererData {
        glm::mat4 ViewMatrix;
        glm::mat4 ProjectionMatrix;
        glm::mat4 ViewProjectionMatrix;
        glm::vec3 CameraPosition;
        
        std::shared_ptr<Texture2D> WhiteTexture;
        std::shared_ptr<Texture2D> BlackTexture;
        std::shared_ptr<Mesh> GridMesh;  // TODO: use in ForwardBlend

        std::shared_ptr<Mesh> SkyboxMesh;
        std::shared_ptr<Shader> SkyboxShader;
        std::shared_ptr<TextureCube> EnvironmentCubemap; 
        std::shared_ptr<TextureCube> IrradianceMap;
        std::shared_ptr<TextureCube> PrefilterMap;
        std::shared_ptr<Texture2D>   BRDFLUT;
        float EnvironmentIntensity = 1.0f;
        glm::vec3 EnvironmentAmbientColor = { 0.1f, 0.1f, 0.1f };
        glm::vec4 ClearColor = { 0.06f, 0.06f, 0.065f, 1.0f };

        struct_CameraData CameraData;
        struct_DirLight DirLight;

        uint32_t ViewportWidth = 1280;
        uint32_t ViewportHeight = 720;
        uint32_t EmptyVAO;

        SceneRenderer::Statistics Stats;
        float LastGPUTime = 0.0f;           // fallback when no timestamps available
        uint32_t GPUTimeQuery = 0;
    };

    SceneRenderer::SceneRenderer() {
        m_Data = std::make_unique<SceneRendererData>();
        m_CameraUniformBuffer = UniformBuffer::Create(sizeof(struct_CameraData), 0);
        m_DirLightUniformBuffer = UniformBuffer::Create(sizeof(struct_DirLight), 1);
        m_PipelineBuilder = std::make_unique<PipelineBuilder>(m_RenderGraph);
    }

    SceneRenderer::~SceneRenderer() = default;

    void SceneRenderer::Init() {
        m_Data->WhiteTexture = Texture2D::Create(1, 1);
        if (m_Data->WhiteTexture) {
            uint32_t whiteTextureData = 0xffffffff;
            m_Data->WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));
        }

        m_Data->BlackTexture = Texture2D::Create(1, 1);
        if (m_Data->BlackTexture) {
            uint32_t blackTextureData = 0x00000000;
            m_Data->BlackTexture->SetData(&blackTextureData, sizeof(uint32_t));
        }

        m_Data->GridMesh = Mesh::CreatePlane(1.0f, 1.0f);

        // ==========================================
        // 【核心防御 1】：隔离 OpenGL 专属的天空盒与 IBL 逻辑
        // ==========================================
        if (!s_SkyboxMesh) {
            // Mesh 是跨平台的，可以直接创建
            s_SkyboxMesh = Mesh::CreateCube(1.0f);

            if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
                // OpenGL 专属逻辑：编译 GLSL 着色器，生成 BRDF 贴图
                s_SkyboxShader = Shader::Create("Skybox/skybox.vert", "Skybox/skybox.frag");
                std::shared_ptr<Shader> brdfShader = Shader::Create("IBL/brdf.vert", "IBL/brdf.frag");
                if(m_Data->EmptyVAO == 0) glGenVertexArrays(1, &m_Data->EmptyVAO);
                void* brdfID = IBLBuilder::CreateBRDFLUT(brdfShader, (void*)(uintptr_t)m_Data->EmptyVAO);
                s_DefaultBRDFLUT = Texture2D::Create(brdfID, 512, 512);
            } 
            else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                // Vulkan 路径：烘焙真正的 BRDF LUT
                std::shared_ptr<Shader> brdfShader = Shader::Create("IBL/brdf.vert", "IBL/brdf.frag");
                void* brdfID = IBLBuilder::CreateBRDFLUT(brdfShader, nullptr);
                s_DefaultBRDFLUT = Texture2D::Create(brdfID, 512, 512);

                // 创建默认 IBL cubemap 占位符（无环境贴图时用作 fallback）
                auto defaultCube = VulkanTextureCube::CreateDefault();
                s_DefaultEnvironmentMap = defaultCube;
                s_DefaultIrradianceMap = defaultCube;
                s_DefaultPrefilterMap = defaultCube;
            }
        }

        m_Data->SkyboxMesh = s_SkyboxMesh;
        m_Data->SkyboxShader = s_SkyboxShader;
        m_Data->BRDFLUT = s_DefaultBRDFLUT;
        
        // 全局 UBO 内存分配 (假设 UniformBuffer::Create 已经做了跨平台处理)
        // if (!s_CameraUniformBuffer) s_CameraUniformBuffer = UniformBuffer::Create(sizeof(struct_CameraData), 0);
        // if (!s_LightUniformBuffer) s_LightUniformBuffer = UniformBuffer::Create(sizeof(struct_LightData), 1);

        // ==========================================
        // 管线初始化 (OpenGL 线性管线 / Vulkan RenderGraph)
        // ==========================================
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            m_Pipeline.AddPass(std::make_shared<ShadowPass>());
            m_Pipeline.AddPass(std::make_shared<GBufferPass>());
            m_Pipeline.AddPass(std::make_shared<LightingPass>());
            m_Pipeline.AddPass(std::make_shared<BloomPass>());
            m_Pipeline.AddPass(std::make_shared<PostProcessPass>());
            m_Pipeline.AddPass(std::make_shared<FXAAPass>());
            // UIPass requires RenderGraph FBO injection — not supported in OpenGL linear pipeline
            m_Pipeline.Init();

            if (m_Data->EmptyVAO == 0) glGenVertexArrays(1, &m_Data->EmptyVAO);
            glGenQueries(1, &m_Data->GPUTimeQuery);
        }
        else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            AYAYA_CORE_INFO("Vulkan Pipeline: RenderGraph Mode");

            // ==========================================
            // Deferred Rendering Pipeline (Vulkan 1.3)
            //    Shadow → GBuffer → Lighting → Bloom → PostProcess → FXAA → UI
            // ==========================================
            m_ShadowPass      = std::make_shared<VulkanShadowPass>();
            m_GBufferPass     = std::make_shared<VulkanGBufferPass>();
            m_DepthPass       = std::make_shared<VulkanDepthPrePass>();
            m_LightingPass    = std::make_shared<VulkanLightingPass>();
            m_ForwardBlendPass = std::make_shared<VulkanForwardBlendPass>();
            m_OutlinePass     = std::make_shared<VulkanOutlinePass>();
            m_BloomPass       = std::make_shared<VulkanBloomPass>();
            m_PostProcessPass = std::make_shared<VulkanPostProcessPass>();
            m_FXAAPass        = std::make_shared<VulkanFXAAPass>();
            m_SSAOPass        = std::make_shared<VulkanSSAOPass>();
            m_UIPass          = std::make_shared<UIPass>();
            m_WBOITPass       = std::make_shared<VulkanWBOITPass>();

            // ── Create shared GDR data hub before pass OnAttach() ──
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            if (vkCtx) {
                // Create GDRContext once (static, shared across all SceneRenderer instances)
                if (!m_GDRContext) {
                    m_GDRContext = std::make_shared<GDRContext>();
                    m_GDRContext->Init(vkCtx->GetDevice(), vkCtx->GetFramesInFlight(),
                                       vkCtx->GetGeometryPool().GetBuffer());
                }
                // Pass shared context to GDR-enabled passes (every renderer's passes share the same one)
                auto shadowPass = std::dynamic_pointer_cast<VulkanShadowPass>(m_ShadowPass);
                auto gbufferPass = std::dynamic_pointer_cast<VulkanGBufferPass>(m_GBufferPass);
                auto depthPass = std::dynamic_pointer_cast<VulkanDepthPrePass>(m_DepthPass);
                if (shadowPass) shadowPass->SetGDRContext(m_GDRContext);
                if (gbufferPass) gbufferPass->SetGDRContext(m_GDRContext);
                if (depthPass) depthPass->SetGDRContext(m_GDRContext);
            }

            // 🔥 Pre-register UBO buffers before pipeline construction.
            //    s_GlobalUBOs must be populated when VulkanPipeline constructs descriptor sets
            //    during OnAttach→Pipeline::Create. Without this, Set 0 descriptor sets are
            //    written with null buffer references → shaders read zero camera/light data → black.
            // Create point light SSBO (triple-buffered, max 65536 lights)
            m_PointLightSSBO = std::make_unique<VulkanStorageBuffer>(
                65536 * sizeof(GPUPointLight),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            // Light volume instance SSBO (mat4 per light, for instanced sphere rendering)
            m_LightInstanceSSBO = std::make_unique<VulkanStorageBuffer>(
                65536 * sizeof(glm::mat4),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

            auto camUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_CameraUniformBuffer);
            auto dirLightUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_DirLightUniformBuffer);
            if (camUBO) for (uint32_t i = 0; i < 3; i++)
                VulkanPipeline::SetGlobalUniformBuffer(0, i, camUBO->GetBuffer(i), sizeof(struct_CameraData));
            if (dirLightUBO) for (uint32_t i = 0; i < 3; i++)
                VulkanPipeline::SetGlobalUniformBuffer(1, i, dirLightUBO->GetBuffer(i), sizeof(struct_DirLight));

            m_ShadowPass->OnAttach();
            m_GBufferPass->OnAttach();
            m_DepthPass->OnAttach();
            m_SSAOPass->OnAttach();
            m_LightingPass->OnAttach();
            m_ForwardBlendPass->OnAttach();
            m_OutlinePass->OnAttach();
            m_BloomPass->OnAttach();
            m_PostProcessPass->OnAttach();
            m_FXAAPass->OnAttach();
            m_UIPass->OnAttach();
            m_WBOITPass->OnAttach();

            // ── Register all passes for SRP (Scriptable Render Pipeline) by-name lookup ──
            PassRegistry::Init(m_GDRContext,
                               m_ShadowPass, m_GBufferPass, m_DepthPass,
                               m_LightingPass, m_ForwardBlendPass,
                               m_SSAOPass, m_OutlinePass, m_BloomPass,
                               m_PostProcessPass, m_FXAAPass, m_UIPass,
                               m_WBOITPass);
        }
    }

    void SceneRenderer::Shutdown() {
        PassRegistry::Shutdown();
        UIPass::Shutdown();
        VulkanSSAOPass::ReleaseNoiseTexture();
        // s_CameraUniformBuffer.reset();
        // s_LightUniformBuffer.reset();
        s_SkyboxMesh.reset();
        s_SkyboxShader.reset();
        s_DefaultEnvironmentMap.reset();
        s_DefaultIrradianceMap.reset();
        s_DefaultPrefilterMap.reset();
        s_DefaultBRDFLUT.reset();
        // GDRContext is per-renderer — cleaned up in SceneRenderer destructor
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            // 销毁所有烘焙出来的 IBL 环境贴图显存
            VulkanIBLBuilder::ClearResources();
        }
        AYAYA_CORE_INFO("SceneRenderer static resources cleared.");
    }

    void SceneRenderer::OnWindowResize(uint32_t width, uint32_t height) {
        if (width == m_Data->ViewportWidth && height == m_Data->ViewportHeight) return;
        m_Data->ViewportWidth = width;
        m_Data->ViewportHeight = height;
        m_ViewportDirty = true;

        // Deferred destruction queue handles old FBO safety — no vkDeviceWaitIdle needed.
        // Old FBOs stay alive for 3 frames after Release(), eliminating GPU stalls on resize.
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL)
            m_Pipeline.OnResize(width, height);
    }
    
    void SceneRenderer::BeginScene(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPosition) {
    ResetStats();

    // 记录原始数据
        m_Data->ViewMatrix = viewMatrix;
        m_Data->ProjectionMatrix = projectionMatrix;
        m_Data->CameraPosition = cameraPosition;

        // GLM_FORCE_DEPTH_ZERO_TO_ONE: glm::perspective produces [0,1] depth already.
        // Vulkan: zero correction — pure projection, no Y-flip, no Z-remap.
        // Negative viewport handles Y-axis (VK spec §27.7 winding auto-compensation).
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            m_RenderContext.ProjectionMatrix = projectionMatrix;
        }
        // OpenGL: GLM produces [0,1] depth, but OpenGL expects [-1,1] → reverse map.
        else {
            static const glm::mat4 openGLDepthCorrection(
                1.0f, 0.0f, 0.0f, 0.0f,   // col 0
                0.0f, 1.0f, 0.0f, 0.0f,   // col 1
                0.0f, 0.0f, 2.0f, 0.0f,   // col 2: z' = 2z
                0.0f, 0.0f,-1.0f, 1.0f    // col 3: w' = w - z  →  NDC' = 2*NDC - 1
            );
            m_RenderContext.ProjectionMatrix = openGLDepthCorrection * projectionMatrix;
        }

        m_RenderContext.ViewMatrix = viewMatrix;
        m_RenderContext.CameraPosition = cameraPosition;

        // ==========================================
        // ViewProjection uses depth-corrected projection (GL [-1,1] → VK [0,1]).
        // ==========================================
        m_Data->ViewProjectionMatrix = m_RenderContext.ProjectionMatrix * viewMatrix;
        m_RenderContext.Set("InverseProj", glm::inverse(m_RenderContext.ProjectionMatrix));
        m_RenderContext.Set("InverseView", glm::inverse(viewMatrix));
        m_RenderContext.Set("InverseViewProj", glm::inverse(m_Data->ViewProjectionMatrix));

        // 3. 填充数据结构
        m_Data->CameraData.ViewProjection = m_Data->ViewProjectionMatrix;
        m_Data->CameraData.View = viewMatrix;
        m_Data->CameraData.CameraPosition = m_Data->CameraPosition;

        // ── Frame constants (Unity convention) ──
        float vpW = (float)m_Data->ViewportWidth;
        float vpH = (float)m_Data->ViewportHeight;
        m_Data->CameraData.ScreenParams = glm::vec4(vpW, vpH, 1.0f + 1.0f / vpW, 1.0f + 1.0f / vpH);
        float t = fmodf((float)glfwGetTime(), 3600.0f);  // mod 3600 prevents float precision loss
        m_Data->CameraData.Time = glm::vec4(t / 20.0f, t, t * 2.0f, t * 3.0f);
        
        // ==========================================
        // 4. 【核心修复】：上传数据到当前实例私有的 UBO
        // ==========================================
        m_CameraUniformBuffer->SetData(&m_Data->CameraData, sizeof(struct_CameraData));
    }

    void SceneRenderer::EndScene() {
    }

    void SceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene, const RenderViewConfig& config) {
        // 统计初始化
        auto cpuStartTime = std::chrono::high_resolution_clock::now();

        // ==========================================
        // 每次渲染前，清空黑板上的统计板
        // ==========================================
        m_RenderContext.Stats.DrawCalls = 0;
        m_RenderContext.Stats.ShaderBinds = 0;
        m_RenderContext.Stats.VertexCount = 0;
        m_RenderContext.Stats.TriangleCount = 0;

        m_RenderContext.FrameSteps.clear(); // 每帧清空流水账本！
        m_RenderContext.PassProfiles.clear();  // 每帧清空性能统计
        m_RenderContext.Framebuffers.clear();  // 每帧清空 FBO 黑板，防止跨帧残留
        
        memset(&m_Data->DirLight, 0, sizeof(struct_DirLight));

        float physicalExposure = 1.0f;
        auto cameraView = scene->Reg().view<CameraComponent>();
        for (auto entityID : cameraView) {
            auto& cameraComp = cameraView.get<CameraComponent>(entityID);
            if (cameraComp.Primary) {
                physicalExposure = 1.0f / (std::exp2(cameraComp.EV100) * 1.2f);
                break;
            }
        }

        bool hasDirLight = false;
        auto dirLightGroup = scene->Reg().view<TransformComponent, DirectionalLightComponent>();
        for (auto entityID : dirLightGroup) {
            auto [transform, dlc] = dirLightGroup.get<TransformComponent, DirectionalLightComponent>(entityID);
            glm::quat orientation = glm::quat(transform.Rotation);
            glm::vec3 dir = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
            
            m_Data->DirLight.DirLightDir = glm::vec4(dir, 0.0f);
            m_Data->DirLight.DirLightColor = glm::vec4(dlc.Color * dlc.Illuminance, 0.0f);
            hasDirLight = true;
            break;
        }

        if (!hasDirLight) {
            m_Data->DirLight.DirLightDir = glm::vec4(0.0f, -1.0f, 0.0f, 0.03f);
        }

        // Point lights — SSBO (unlimited count, frustum-culled)
        {
            std::vector<GPUPointLight> gpuLights;
            glm::mat4 vp = m_RenderContext.ProjectionMatrix * m_RenderContext.ViewMatrix;
            Frustum frustum(vp);
            auto pointLightGroup = scene->Reg().view<TransformComponent, PointLightComponent>();
            for (auto entityID : pointLightGroup) {
                auto [transform, plc] = pointLightGroup.get<TransformComponent, PointLightComponent>(entityID);
                // CPU frustum culling — skip lights entirely outside the view
                // Build AABB from the light's bounding sphere for Frustum::IsBoxVisible
                AABB lightAABB;
                lightAABB.Min = transform.Translation - glm::vec3(plc.Radius);
                lightAABB.Max = transform.Translation + glm::vec3(plc.Radius);
                if (!frustum.IsBoxVisible(lightAABB, glm::mat4(1.0f))) continue;
                float candelas = plc.LuminousPower / (4.0f * glm::pi<float>());
                gpuLights.push_back({
                    glm::vec4(transform.Translation, plc.Radius),
                    glm::vec4(plc.Color * candelas, plc.Falloff)
                });
            }
            uint32_t lightCount = (uint32_t)gpuLights.size();
            if (m_PointLightSSBO) {
                // SSBO layout: [uint count][3*uint _pad][GPUPointLight lights[]]
                // Must write count + pad + light data as contiguous block
                struct { uint32_t count; uint32_t _pad[3]; } header = { lightCount, {} };
                std::vector<uint8_t> buffer(sizeof(header) + lightCount * sizeof(GPUPointLight));
                memcpy(buffer.data(), &header, sizeof(header));
                if (lightCount > 0)
                    memcpy(buffer.data() + sizeof(header), gpuLights.data(),
                           lightCount * sizeof(GPUPointLight));
                m_PointLightSSBO->SetData(buffer.data(), buffer.size());
            }
            m_RenderContext.Set("PointLightCount", (int)lightCount);
            // Pass SSBO buffers to LightingPass for per-frame binding
            if (m_PointLightSSBO && m_LightInstanceSSBO) {
                auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                    Application::Get().GetWindow().GetContext());
                if (vkCtx) {
                    uint32_t fi = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();
                    m_RenderContext.Set("PointLightSSBO", (uint64_t)m_PointLightSSBO->GetBuffer(fi));

                    // Upload instance matrices (translate * scale per light)
                    std::vector<glm::mat4> instances(lightCount);
                    for (uint32_t i = 0; i < lightCount; i++) {
                        glm::vec3 pos = glm::vec3(gpuLights[i].positionAndRadius);
                        float radius = gpuLights[i].positionAndRadius.w;
                        instances[i] = glm::translate(glm::mat4(1.0f), pos)
                                     * glm::scale(glm::mat4(1.0f), glm::vec3(radius));
                    }
                    m_LightInstanceSSBO->SetData(instances.data(),
                        lightCount * sizeof(glm::mat4));
                    m_RenderContext.Set("InstanceSSBO", (uint64_t)m_LightInstanceSSBO->GetBuffer(fi));
                }
            }
        }
        m_DirLightUniformBuffer->SetData(&m_Data->DirLight, sizeof(struct_DirLight));

        // ==========================================
        // GDR: Blind Submit with 3-suspect diagnostics
        // ==========================================
        auto tCull0 = std::chrono::high_resolution_clock::now();
        if (m_GDRContext && RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            if (vkCtx) {
                uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();
                m_GDRContext->BuildFromScene(scene.get(), vkCtx->GetGeometryPool(), frameIdx);
            }
        }
        auto tCull1 = std::chrono::high_resolution_clock::now();


        // ==========================================
        // RenderQueue: translucent only
        // GPUInstanceIndex MUST match BuildFromScene order:
        //   BuildFromScene iterates the same ECS view, skips inactive entities,
        //   and pushes ONE GDR instance PER SUB-MESH.
        //   The RenderQueue must mirror this exactly — increment gpuIdx only
        //   for active entities AND per sub-mesh.
        // ==========================================
        m_RenderQueue.Clear();
        {
            glm::mat4 viewProj = m_RenderContext.ProjectionMatrix * m_RenderContext.ViewMatrix;
            Frustum frustum(viewProj);
            auto view = scene->Reg().view<TransformComponent, MeshRendererComponent>();
            uint32_t gpuIdx = 0;  // tracks GDR SSBO instance index (matches BuildFromScene order)
            for (auto entityID : view) {
                Entity entity{ entityID, scene.get() };
                if (!entity.IsActiveInHierarchy()) continue;  // must check BEFORE gpuIdx — BuildFromScene does too
                auto& meshComp = view.get<MeshRendererComponent>(entityID);
                if (!meshComp.CachedMaterial)
                    meshComp.CachedMaterial = AssetManager::GetAsset<Material>(meshComp.MaterialHandle);
                auto material = meshComp.CachedMaterial;
                uint8_t bucket = material ? material->GetRenderBucket() : 0;
                if (bucket <= 1) {
                    // Still need to advance gpuIdx for opaque/masked entities —
                    // BuildFromScene creates GDR instances for ALL blend modes.
                    auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
                    if (model) gpuIdx += (uint32_t)model->GetMeshes().size();
                    continue;
                }
                auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
                if (!model) continue;
                glm::mat4 transform = entity.GetWorldTransform();
                for (auto& mesh : model->GetMeshes()) {
                    uint32_t instIdx = gpuIdx++;  // per-sub-mesh, same as BuildFromScene
                    if (!frustum.IsBoxVisible(mesh->GetAABB(), transform)) continue;
                    DrawPacket packet;
                    packet.Transform = transform;
                    packet.CastShadows = meshComp.CastShadows;
                    packet.ReceiveShadows = meshComp.ReceiveShadows;
                    packet.MeshAsset = mesh;
                    packet.MaterialAsset = material;
                    packet.GPUInstanceIndex = instIdx;
                    SortKey key; key.Value = 0;
                    key.Bits.BucketID = bucket;
                    key.Bits.MaterialHash = (meshComp.MaterialHandle & 0xFFF);
                    key.Bits.EntityID = static_cast<uint32_t>(entityID) & 0xFFFF;
                    key.Bits.Depth = 0;
                    packet.SortKey = key.Value;
                    m_RenderQueue.Packets.push_back(packet);
                }
            }
        }
        m_RenderQueue.Sort();
        auto tGDR = std::chrono::high_resolution_clock::now();

        // AYAYA_CORE_INFO("[RenderQueue] Extracted {} draw packets from {} entities",
        //     m_RenderQueue.Packets.size(),
        //     scene->Reg().view<TransformComponent, MeshRendererComponent>().size_hint());

        // Inject render queue into context for passes to consume
        m_RenderContext.RenderQueue = &m_RenderQueue;

        // ==========================================
        // 布置 Render Graph 数据黑板并轰鸣管线！
        // ==========================================
        m_RenderContext.ActiveScene = scene;
        m_RenderContext.Set("ViewportWidth", m_Data->ViewportWidth);
        m_RenderContext.Set("ViewportHeight", m_Data->ViewportHeight);
        m_RenderContext.Set("HasDirLight", hasDirLight);
        m_RenderContext.Set("DirLightDir", glm::vec3(m_Data->DirLight.DirLightDir));

        m_RenderContext.SetTexture("WhiteTexture", m_Data->WhiteTexture);
        m_RenderContext.SetTexture("BlackTexture", m_Data->BlackTexture);
        m_RenderContext.Set<std::shared_ptr<Mesh>>("SkyboxMesh", m_Data->SkyboxMesh);
        m_RenderContext.Set<std::shared_ptr<Mesh>>("GridMesh", m_Data->GridMesh);
        m_RenderContext.SetTexture("BRDFLUT", m_Data->BRDFLUT);
        m_RenderContext.Set<std::shared_ptr<TextureCube>>("EnvironmentCubemap", m_Data->EnvironmentCubemap);
        m_RenderContext.Set<std::shared_ptr<TextureCube>>("IrradianceMap",
            m_Data->IrradianceMap ? m_Data->IrradianceMap : s_DefaultIrradianceMap);
        m_RenderContext.Set<std::shared_ptr<TextureCube>>("PrefilterMap",
            m_Data->PrefilterMap ? m_Data->PrefilterMap : s_DefaultPrefilterMap);

        m_RenderContext.Set("ClearColor", config.ClearColor);
        m_RenderContext.Set("ShowSkybox", config.EnableSkybox);
        m_RenderContext.Set("ShowGrid", config.EnableGrid && config.IsEditorView);
        m_RenderContext.Set("HoveredEntity", config.HoveredEntity);
        m_RenderContext.Set("SelectedEntities", config.SelectedEntities);
        m_RenderContext.Set("EnvironmentIntensity", m_Data->EnvironmentIntensity);
        m_RenderContext.Set("EnvironmentAmbientColor", m_Data->EnvironmentAmbientColor);

        m_RenderContext.Set("PhysicalExposure", physicalExposure);
        // ==========================================
        // ECS 驱动的后处理体积 (Post-Process Volume)
        // ==========================================
        // 赋予默认值 (防止场景里没有任何 Volume)
        int   tmType = 0;
        float exposure = 1.0f;
        bool  enableBloom = false;
        float bThreshold = 1.0f, bKnee = 0.1f, bRadius = 0.005f, bIntensity = 1.0f;
        bool  enableFXAA = false;
        bool  enableSSAO = false;
        float ssaoRadius = 0.5f, ssaoBias = 0.025f;

        // 遍历场景，寻找全局的 Volume
        auto volumeView = scene->Reg().view<PostProcessVolumeComponent>();
        for (auto entityID : volumeView) {
            auto& volume = volumeView.get<PostProcessVolumeComponent>(entityID);
            if (volume.IsGlobal) {
                tmType = volume.ToneMappingType;
                exposure = volume.Exposure;
                enableBloom = volume.EnableBloom;
                bThreshold = volume.BloomThreshold;
                bKnee = volume.BloomKnee;
                bRadius = volume.BloomRadius;
                bIntensity = volume.BloomIntensity;
                enableFXAA = volume.EnableFXAA;
                enableSSAO = volume.EnableSSAO;
                ssaoRadius = volume.SSAORadius;
                ssaoBias   = volume.SSAOBias;
                break;
            }
        }

        m_RenderContext.Set("ToneMappingType", tmType);
        m_RenderContext.Set("ExposureCompensation", exposure);
        m_RenderContext.Set("EnableBloom", enableBloom);
        m_RenderContext.Set("BloomThreshold", bThreshold);
        m_RenderContext.Set("BloomKnee", bKnee);
        m_RenderContext.Set("BloomRadius", bRadius);
        m_RenderContext.Set("BloomIntensity", bIntensity);
        m_RenderContext.Set("EnableFXAA", enableFXAA);
        m_RenderContext.Set("EnableSSAO", enableSSAO);
        m_RenderContext.Set("SSAORadius", ssaoRadius);
        m_RenderContext.Set("SSAOBias", ssaoBias);

        // ── Inject SRP global shader params ──
        if (m_PipelineBuilder) {
            const auto* globals = m_PipelineBuilder->GetBakedParams("__Globals__");
            if (globals) {
                for (auto& [k, v] : globals->FloatParams)
                    m_RenderContext.SetGlobalFloat(k, v);
                for (auto& [k, v] : globals->IntParams)
                    m_RenderContext.SetGlobalInt(k, v);
            }
        }

        std::shared_ptr<RenderCommandBuffer> cmd = RenderCommandBuffer::Create();

        if (cmd) {
            cmd->Begin();

            if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                uint32_t vpW = m_Data->ViewportWidth;
                uint32_t vpH = m_Data->ViewportHeight;

                auto tGraph0 = std::chrono::high_resolution_clock::now();
                BuildRenderGraph(config, vpW, vpH);
                auto tGraph1 = std::chrono::high_resolution_clock::now();

                VulkanPipeline::ClearGlobalUBOs();
                auto camUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_CameraUniformBuffer);
                auto dirLightUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_DirLightUniformBuffer);
                if (camUBO) for (uint32_t i=0;i<3;i++) VulkanPipeline::SetGlobalUniformBuffer(0,i,camUBO->GetBuffer(i),sizeof(struct_CameraData));
                if (dirLightUBO) for (uint32_t i=0;i<3;i++) VulkanPipeline::SetGlobalUniformBuffer(1,i,dirLightUBO->GetBuffer(i),sizeof(struct_DirLight));

                // Read N-1 frame GPU timestamps before executing current frame
                auto tsCtx = std::dynamic_pointer_cast<VulkanContext>(
                    Application::Get().GetWindow().GetContext());
                if (tsCtx) tsCtx->ReadTimestampResults();

                auto tExec0 = std::chrono::high_resolution_clock::now();
                m_RenderGraph.Execute(m_RenderContext, *cmd);
                auto tExec1 = std::chrono::high_resolution_clock::now();

                // ── Per-RenderScene timing log — uncomment to debug pass cost ──
                //static int s_SceneFrameIdx = 0;
                //if (++s_SceneFrameIdx % 60 == 0) {
                //    using ms = std::chrono::duration<float, std::milli>;
                //    float gdrMs   = std::chrono::duration_cast<ms>(tCull1 - tCull0).count();
                //    float queueMs = std::chrono::duration_cast<ms>(tGDR - tCull1).count();
                //    float graphMs = std::chrono::duration_cast<ms>(tGraph1 - tGraph0).count();
                //    float execMs  = std::chrono::duration_cast<ms>(tExec1 - tExec0).count();
                //    float uboMs   = std::chrono::duration_cast<ms>(tExec0 - tGraph1).count();
                //    AYAYA_CORE_INFO("[Scene] gdr={:.3f}ms queue={:.3f}ms graph={:.3f}ms ubo={:.3f}ms exec={:.3f}ms gdrInst={} qPackets={}",
                //        gdrMs, queueMs, graphMs, uboMs, execMs,
                //        m_GDRContext ? (int)m_GDRContext->InstanceCount : 0,
                //        (int)m_RenderQueue.Packets.size());
                //}
            } else {
                // OpenGL 线性管线 (保持兼容)
                m_Pipeline.Execute(m_RenderContext, *cmd);
            }

            cmd->End();
        } else {
            AYAYA_CORE_ERROR("Failed to create RenderCommandBuffer in RenderScene!");
        }

        // 【新增】：将管线里各个 Pass 汇报的成绩，抄写到总管的统计表里供 UI 显示
        m_Data->Stats.DrawCalls = m_RenderContext.Stats.DrawCalls;
        m_Data->Stats.ShaderBinds = m_RenderContext.Stats.ShaderBinds;
        m_Data->Stats.TriangleCount = m_RenderContext.Stats.TriangleCount;
        m_Data->Stats.VertexCount = m_RenderContext.Stats.VertexCount;

        // ── GDR diagnostics ──
        if (m_GDRContext) {
            m_Data->Stats.GDRInstanceCount = m_GDRContext->InstanceCount;
            m_Data->Stats.GDRRangeCount    = m_GDRContext->RangeCount;
            m_Data->Stats.GDRMaterialCount = m_GDRContext->MaterialCount;
        }

        // 性能统计
        float totalGPUTime = 0.0f;
        for (const auto& [name, profile] : m_RenderContext.PassProfiles) {
            totalGPUTime += profile.GPUTime;
        }
        // 若当前帧无有效时间戳，回退到上一帧的已知值（GPU 压力下可能出现）
        if (totalGPUTime == 0.0f && m_Data->LastGPUTime > 0.0f) {
            totalGPUTime = m_Data->LastGPUTime;
        } else if (totalGPUTime > 0.0f) {
            m_Data->LastGPUTime = totalGPUTime;
        }
        m_Data->Stats.GPUTime = totalGPUTime;

        // 结算真正的 C++ 准备指令耗时
        auto cpuEndTime = std::chrono::high_resolution_clock::now();
        m_Data->Stats.CPUTime = std::chrono::duration<float, std::milli>(cpuEndTime - cpuStartTime).count();
    }

    void* SceneRenderer::GetFinalColorAttachmentRendererID() {
        auto tryFBO = [&](const char* key) -> void* {
            auto it = m_RenderContext.Framebuffers.find(key);
            if (it != m_RenderContext.Framebuffers.end() && it->second)
                return it->second->GetColorAttachmentRendererID(0);
            return nullptr;
        };
        // Dynamic export (BuildRenderGraph sets this)
        if (!m_FinalExportTexture.empty())
            if (void* id = tryFBO(m_FinalExportTexture.c_str())) return id;
        if (void* id = tryFBO("FXAA"))         return id;
        if (void* id = tryFBO("FinalOutput"))   return id;
        if (void* id = tryFBO("GBuffer"))       return id;
        if (void* id = tryFBO("Lighting"))      return id;
        return nullptr;
    }

    void* SceneRenderer::GetPostProcessFBORendererID() {
        return GetFinalColorAttachmentRendererID();
    }

    void* SceneRenderer::GetBlackboardTextureID(std::string_view key) {
        std::string keyStr(key);
        
        // 1. 最安全路径：直接从强类型的 FBO 字典拿 (避开 std::any)
        if (m_RenderContext.Framebuffers.find(keyStr) != m_RenderContext.Framebuffers.end()) {
            auto fbo = m_RenderContext.Framebuffers[keyStr];
            if (fbo) return fbo->GetColorAttachmentRendererID(0);
        }

        // 2. 兼容路径：既然 Data 是私有的，我们用 try-catch 安全探测类型！
        // 只要类型不对，std::any 就会抛出异常，我们接住它然后试下一个，绝对不会闪退！
        
        try {
            // 尝试 A: 存的是明确的 Framebuffer 智能指针
            auto fbo = m_RenderContext.Get<std::shared_ptr<Framebuffer>>(keyStr, nullptr);
            if (fbo) return fbo->GetColorAttachmentRendererID(0);
        } catch (const std::bad_any_cast&) {} // 抓到异常就默默忽略，试下一个

        try {
            // 尝试 B: 存的就是原生指针 (如 GBuffer_Position)
            return m_RenderContext.Get<void*>(keyStr, nullptr);
        } catch (const std::bad_any_cast&) {}
        
        try {
            // 尝试 C: 存的是被擦除类型的 void 智能指针 (历史遗留)
            auto ptr = m_RenderContext.Get<std::shared_ptr<void>>(keyStr, nullptr);
            if (ptr) {
                auto fbo = std::static_pointer_cast<Framebuffer>(ptr);
                if (fbo) return fbo->GetColorAttachmentRendererID(0);
            }
        } catch (const std::bad_any_cast&) {}

        return nullptr; // 没找到或者类型全都不匹配，安全返回空
    }

    void SceneRenderer::ResetStats() {
        memset(&m_Data->Stats, 0, sizeof(Statistics));
    }

    SceneRenderer::Statistics SceneRenderer::GetStats() {
        return m_Data->Stats;
    }

    void SceneRenderer::SetMSAASamples(uint32_t samples) {
        m_RenderContext.Set("MSAASamples", 1u);
    }

    void SceneRenderer::SetEnvironment(EnvironmentComponent& envComp) {
        if (envComp.Type == EnvironmentType::None) {
            m_Data->EnvironmentCubemap = nullptr;
            m_Data->IrradianceMap = nullptr;
            m_Data->PrefilterMap = nullptr;
            envComp.IsDirty = false;
            return;
        }

        // 先构建新纹理到局部变量，成功后再原子替换 m_Data
        // 避免旧纹理过早销毁导致渲染 Pass 绑定已释放的 VkImageView
        std::shared_ptr<TextureCube> newEnvCubemap;
        std::shared_ptr<TextureCube> newIrradiance;
        std::shared_ptr<TextureCube> newPrefilter;
        void* baseCubemapID = nullptr;

        if (envComp.Type == EnvironmentType::HDR_Equirectangular || envComp.Type == EnvironmentType::LDR_Equirectangular) {
            auto equiTex = AssetManager::GetAsset<Texture2D>(envComp.EquirectangularHandle);
            if (equiTex) {
                std::shared_ptr<Shader> convertShader = Shader::Create("IBL/equirectangular_to_cubemap.vert", "IBL/equirectangular_to_cubemap.frag");
                baseCubemapID = IBLBuilder::ConvertEquirectangularToCubemap(equiTex, s_SkyboxMesh, convertShader);
                newEnvCubemap = TextureCube::Create(baseCubemapID, 1024, 1024);

                if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                    auto vkCube = std::dynamic_pointer_cast<VulkanTextureCube>(newEnvCubemap);
                    VulkanIBLBuilder::SetSourceCubemapSampler((void*)vkCube->GetSampler());
                }
            } else {
                // HDR 尚未加载完成，保持 IsDirty=true，下一帧重试，旧纹理继续有效
                return;
            }
        }
        else if (envComp.Type == EnvironmentType::Classic_Cubemap) {
            auto cubeTex = AssetManager::GetAsset<TextureCube>(envComp.CubemapHandle);
            if (!cubeTex) return; // asset not ready — keep IsDirty=true, retry next frame

            newEnvCubemap = cubeTex;

            if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                auto vkCube = std::dynamic_pointer_cast<VulkanTextureCube>(cubeTex);
                baseCubemapID = (void*)vkCube->GetImageView();  // 完整 64 位 VkImageView
                VulkanIBLBuilder::SetSourceCubemapSampler((void*)vkCube->GetSampler());
            } else {
                baseCubemapID = (void*)(uintptr_t)cubeTex->GetRendererID();
                uint32_t glTextureID = (uint32_t)(uintptr_t)baseCubemapID;
                glBindTexture(GL_TEXTURE_CUBE_MAP, glTextureID);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            }
        }

        if (baseCubemapID != nullptr) {
            std::shared_ptr<Shader> irradianceShader = Shader::Create("IBL/cubemap.vert", "IBL/irradiance_convolution.frag");
            void* irrID = IBLBuilder::CreateIrradianceMap(baseCubemapID, s_SkyboxMesh, irradianceShader);
            newIrradiance = TextureCube::Create(irrID, 32, 32);

            std::shared_ptr<Shader> prefilterShader = Shader::Create("IBL/cubemap.vert", "IBL/prefilter.frag");
            void* preID = IBLBuilder::CreatePrefilterMap(baseCubemapID, s_SkyboxMesh, prefilterShader);
            newPrefilter = TextureCube::Create(preID, 128, 128);

            // Old cubemaps: push to deferred queue (3-frame delay via VulkanContext fence).
            // The shared_ptr keeps the TextureCube alive until the lambda fires.
            if (m_Data->EnvironmentCubemap) {
                auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                    Application::Get().GetWindow().GetContext());
                if (vkCtx) {
                    auto oldEnv   = m_Data->EnvironmentCubemap;
                    auto oldIrr   = m_Data->IrradianceMap;
                    auto oldPref  = m_Data->PrefilterMap;
                    vkCtx->QueueDeferredResource(VulkanContext::DeferredResource{
                        {[oldEnv, oldIrr, oldPref]() {
                            // shared_ptr drops → destructor frees GPU resources
                        }}
                    });
                }
            }

            m_Data->EnvironmentCubemap = newEnvCubemap;
            m_Data->IrradianceMap = newIrradiance;
            m_Data->PrefilterMap = newPrefilter;

            envComp.IsDirty = false;
        }
    }

    void SceneRenderer::SetEnvironmentSettings(float intensity, const glm::vec3& ambientColor) {
        m_Data->EnvironmentIntensity = intensity;
        m_Data->EnvironmentAmbientColor = ambientColor;
    }

    void SceneRenderer::SetClearColor(const glm::vec4& color) {
        m_Data->ClearColor = color;
    }

    // ==========================================
    // BuildRenderGraph — 5阶段动态组装
    // ==========================================
    // Helper: find a pass by name in the compiled pass list and update its IsCulled flag
    void SceneRenderer::BuildRenderGraph(const RenderViewConfig& config, uint32_t vpW, uint32_t vpH) {
        if (m_SRPScriptHandle != 0)
            BuildRenderGraph_SRP(config, vpW, vpH);
        else
            BuildRenderGraph_Default(config, vpW, vpH);
    }

    void SceneRenderer::BuildRenderGraph_Default(const RenderViewConfig& config, uint32_t vpW, uint32_t vpH) {
        // Pass addition only on viewport resize (expensive — 12 shared_ptr allocations)
        if (m_ViewportDirty) {
            m_RenderGraph.Clear();

            m_RenderGraph.AddPass("ShadowPass",
                [&](RGBuilder& b) { VulkanShadowPass::DeclareResources(b); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_ShadowPass) m_ShadowPass->Execute(ctx, c); });
            m_RenderGraph.AddPass("DepthPrePass",
                [&](RGBuilder& b) { VulkanDepthPrePass::DeclareResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_DepthPass) m_DepthPass->Execute(ctx, c); });
            m_RenderGraph.AddPass("GBufferPass",
                [&](RGBuilder& b) { VulkanGBufferPass::DeclareResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) {
                    ctx.Set("GBuffer.ClearDepth", 0); // LOAD pre-filled depth
                    if (m_GBufferPass) m_GBufferPass->Execute(ctx, c);
                });

            m_RenderGraph.AddPass("SSAOPass",
                [&](RGBuilder& b) { VulkanSSAOPass::DeclareResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_SSAOPass) m_SSAOPass->Execute(ctx, c); });

            m_RenderGraph.AddPass("LightingPass",
                [&](RGBuilder& b) {
                    VulkanLightingPass::DeclareResources(b, vpW, vpH);
                    b.ReadTexture("SSAO_Final");
                },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_LightingPass) m_LightingPass->Execute(ctx, c); });

            m_RenderGraph.AddPass("ForwardBlend",
                [&](RGBuilder& b) { VulkanForwardBlendPass::DeclareResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_ForwardBlendPass) m_ForwardBlendPass->Execute(ctx, c); });

            m_RenderGraph.AddPass("WBOIT_Gather",
                [&](RGBuilder& b) { VulkanWBOITPass::DeclareGatherResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_WBOITPass) m_WBOITPass->ExecuteGather(ctx, c); });

            m_RenderGraph.AddPass("WBOIT_Resolve",
                [&](RGBuilder& b) { VulkanWBOITPass::DeclareResolveResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_WBOITPass) m_WBOITPass->ExecuteResolve(ctx, c); });

            m_RenderGraph.AddPass("OutlinePass",
                [&](RGBuilder& b) { VulkanOutlinePass::DeclareResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_OutlinePass) m_OutlinePass->Execute(ctx, c); });

            m_RenderGraph.AddPass("BloomPass",
                [&](RGBuilder& b) { VulkanBloomPass::DeclareResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_BloomPass) m_BloomPass->Execute(ctx, c); });

            m_RenderGraph.AddPass("PostProcessPass",
                [&](RGBuilder& b) { VulkanPostProcessPass::DeclareResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_PostProcessPass) m_PostProcessPass->Execute(ctx, c); });

            m_RenderGraph.AddPass("FXAAPass",
                [&](RGBuilder& b) { VulkanFXAAPass::DeclareResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_FXAAPass) m_FXAAPass->Execute(ctx, c); });

            m_RenderGraph.AddPass("UIPass",
                [&](RGBuilder& b) { UIPass::DeclareResources(b, vpW, vpH); },
                [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_UIPass) m_UIPass->Execute(ctx, c); });

            m_FinalExportTexture = "FXAA";
            m_ViewportDirty = false;
        }

        // Per-frame: unified dynamic culling (SSAO/Bloom/Outline/WBOIT)
        ApplyPerFrameCulling();
        m_RenderGraph.Compile();
    }

    // ==========================================
    // SRP (Scriptable Render Pipeline) — data-driven pipeline from Lua
    // ==========================================
    void SceneRenderer::SetSRPScript(UUID handle) {
        m_SRPScriptHandle = handle;
        MarkSRPDirty();  // force rebuild even for same-handle "reload"
    }

    void SceneRenderer::BuildRenderGraph_SRP(const RenderViewConfig& config, uint32_t vpW, uint32_t vpH) {
        if (!m_SRPScriptHandle) return;

        // ── Resolve script path ──
        std::string scriptPath = AssetManager::GetAssetPhysicalPath(m_SRPScriptHandle);
        if (scriptPath.empty()) {
            AYAYA_CORE_ERROR("[SRP] Pipeline script not found for handle {} — downgrading to default pipeline",
                (uint64_t)m_SRPScriptHandle);
            // Same-frame downgrade: skip SetSRPScript(0) to avoid MarkSRPDirty(),
            // directly build the default pipeline THIS frame — no black frame.
            m_SRPScriptHandle = 0;
            m_ViewportDirty = true;
            BuildRenderGraph_Default(config, vpW, vpH);
            return;
        }

        // ── Fast path: graph is clean, just update culling + recompile ──
        if (!m_SRPDirty) {
            ApplyPerFrameCulling();
            m_RenderGraph.Compile();
            return;
        }

        // ── Full rebuild ──
        // 🔥 Wait for GPU before destroying old FBOs. With 3 frames-in-flight,
        // command buffers from up to 2 frames ago may still reference them.
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (vkCtx) vkDeviceWaitIdle(vkCtx->GetDevice());

        auto snapshot = m_RenderGraph.ExtractState();

        m_PipelineBuilder = std::make_unique<PipelineBuilder>(m_RenderGraph);
        m_PipelineBuilder->SetViewportSize(vpW, vpH);
        m_PipelineBuilder->SetGDRContext(m_GDRContext);

        // 🔥 Register THIS renderer's pass instances (not global registry).
        // Each SceneRenderer has its own passes with per-renderer UBO bindings.
        m_PipelineBuilder->RegisterPassInstance("ShadowPass",      m_ShadowPass);
        m_PipelineBuilder->RegisterPassInstance("GBufferPass",     m_GBufferPass);
        m_PipelineBuilder->RegisterPassInstance("DepthPrePass",    m_DepthPass);
        m_PipelineBuilder->RegisterPassInstance("SSAOPass",        m_SSAOPass);
        m_PipelineBuilder->RegisterPassInstance("LightingPass",    m_LightingPass);
        m_PipelineBuilder->RegisterPassInstance("ForwardBlend",    m_ForwardBlendPass);
        m_PipelineBuilder->RegisterPassInstance("WBOIT_Gather",   nullptr, m_WBOITPass);
        m_PipelineBuilder->RegisterPassInstance("WBOIT_Resolve",  nullptr, m_WBOITPass);
        m_PipelineBuilder->RegisterPassInstance("OutlinePass",     m_OutlinePass);
        m_PipelineBuilder->RegisterPassInstance("BloomPass",       m_BloomPass);
        m_PipelineBuilder->RegisterPassInstance("PostProcessPass", m_PostProcessPass);
        m_PipelineBuilder->RegisterPassInstance("FXAAPass",        m_FXAAPass);
        m_PipelineBuilder->RegisterPassInstance("UIPass",          m_UIPass);

        auto& luaState = ScriptEngine::GetLuaState();
        luaState["Pipeline"] = m_PipelineBuilder.get();

        try {
            auto result = luaState.safe_script_file(scriptPath, sol::script_pass_on_error);
            if (!result.valid()) {
                sol::error err = result;
                throw std::runtime_error(err.what());
            }

            m_PipelineBuilder->ValidateTextures();
            m_PipelineBuilder->Compile();

            // Apply per-frame culling immediately so the first frame after rebuild is correct.
            // FBOs already created by Compile() above → second Compile() is O(N+E) topology only.
            ApplyPerFrameCulling();
            m_RenderGraph.Compile();

            m_FinalExportTexture = m_PipelineBuilder->GetOutput();
            if (m_FinalExportTexture.empty())
                m_FinalExportTexture = "FXAA";

            m_SRPDirty = false;
            m_ViewportDirty = false;

        } catch (const std::exception& e) {
            AYAYA_CORE_ERROR("[SRP] Compilation failed: {}", e.what());
            AYAYA_CORE_WARN("[SRP] Restoring previous valid render graph.");

            // 🔥 Restore old state — FBOs, passes, and layout tracking intact
            m_RenderGraph.RestoreState(std::move(snapshot));
            m_SRPDirty = false;
        }

        luaState["Pipeline"] = sol::lua_nil;
    }

    void SceneRenderer::ApplyPerFrameCulling() {
        // Per-frame dynamic culling by PassType — unified for both default and SRP paths.
        // PassType is the factory registration name (e.g. "SSAOPass"), set during AddPass.
        // If PassType is empty (hardcoded default path), fall back to Name matching.

        bool enableSSAO    = m_RenderContext.Get<bool>("EnableSSAO", false);
        bool enableBloom   = m_RenderContext.Get<bool>("EnableBloom", true);
        bool enableOutline = m_RenderContext.Get<bool>("EnableOutline", false);

        bool hasTranslucent = false;
        if (m_RenderContext.RenderQueue) {
            for (auto& p : m_RenderContext.RenderQueue->Packets) {
                SortKey k; k.Value = p.SortKey;
                if (k.Bits.BucketID == static_cast<uint64_t>(RenderBucket::Translucent))
                    { hasTranslucent = true; break; }
            }
        }

        for (auto& pass : m_RenderGraph.GetPasses()) {
            const std::string& type = pass->PassType.empty() ? pass->Name : pass->PassType;
            if (type == "SSAOPass")        pass->IsCulled = !enableSSAO;
            if (type == "BloomPass")       pass->IsCulled = !enableBloom;
            if (type == "OutlinePass")     pass->IsCulled = !enableOutline;
            if (type == "WBOIT_Gather")    pass->IsCulled = !hasTranslucent;
            if (type == "WBOIT_Resolve")   pass->IsCulled = !hasTranslucent;
        }
    }

    void SceneRenderer::AddCustomPostProcess(std::shared_ptr<CustomPostProcess> pass) {
        m_CustomPostProcesses.push_back(std::move(pass));
    }
    void SceneRenderer::RemoveCustomPostProcess(const std::string& name) {
        // reserved
    }
}