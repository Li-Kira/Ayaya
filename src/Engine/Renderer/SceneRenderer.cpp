#include "ayapch.h"
#include <any>

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
#include "Renderer/Passes/VulkanLightingPass.hpp"
#include "Renderer/Passes/VulkanPostProcessPass.hpp"
#include "Renderer/Passes/VulkanForwardTestPass.hpp"
#include "Renderer/Passes/VulkanForwardBlendPass.hpp"
#include "Renderer/Passes/VulkanOutlinePass.hpp"
#include "Renderer/Passes/VulkanOutlinePass.hpp"
#include "Renderer/Passes/VulkanShadowPass.hpp"
#include "Renderer/Passes/VulkanBloomPass.hpp"
#include "Renderer/Passes/VulkanFXAAPass.hpp"

#include "Core/Application.hpp"
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
        glm::mat4 ViewProjection; // 64 bytes
        glm::vec3 CameraPosition; // 12 bytes
        float _padding;           // 4 bytes
    };

    struct struct_PointLight {
        glm::vec4 Position; 
        glm::vec4 Color;    
    };

    struct struct_LightData {
        glm::vec4 DirLightDir;   
        glm::vec4 DirLightColor; 
        
        struct_PointLight PointLights[4]; 
        int PointLightCount;              
        int _padding[3];                  
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
        struct_LightData LightData;

        uint32_t ViewportWidth = 1280;
        uint32_t ViewportHeight = 720;
        uint32_t EmptyVAO;

        SceneRenderer::Statistics Stats;
        uint32_t GPUTimeQuery = 0;
    };

    SceneRenderer::SceneRenderer() {
        m_Data = std::make_unique<SceneRendererData>();
        m_CameraUniformBuffer = UniformBuffer::Create(sizeof(struct_CameraData), 0);
        m_LightUniformBuffer = UniformBuffer::Create(sizeof(struct_LightData), 1);
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
            m_LightingPass    = std::make_shared<VulkanLightingPass>();
            m_ForwardBlendPass = std::make_shared<VulkanForwardBlendPass>();
            m_OutlinePass     = std::make_shared<VulkanOutlinePass>();
            m_BloomPass       = std::make_shared<VulkanBloomPass>();
            m_PostProcessPass = std::make_shared<VulkanPostProcessPass>();
            m_FXAAPass        = std::make_shared<VulkanFXAAPass>();
            m_UIPass          = std::make_shared<UIPass>();

            m_ShadowPass->OnAttach();
            m_GBufferPass->OnAttach();
            m_LightingPass->OnAttach();
            m_ForwardBlendPass->OnAttach();
            m_OutlinePass->OnAttach();
            m_BloomPass->OnAttach();
            m_PostProcessPass->OnAttach();
            m_FXAAPass->OnAttach();
            m_UIPass->OnAttach();
        }
    }

    void SceneRenderer::Shutdown() {
        UIPass::Shutdown();
        // s_CameraUniformBuffer.reset();
        // s_LightUniformBuffer.reset();
        s_SkyboxMesh.reset();
        s_SkyboxShader.reset();
        s_DefaultEnvironmentMap.reset();
        s_DefaultIrradianceMap.reset();
        s_DefaultPrefilterMap.reset();
        s_DefaultBRDFLUT.reset();
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

        // 等待 GPU 完成所有飞行中的帧，确保旧 FBO 不再被引用
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
            if (vkCtx) vkDeviceWaitIdle(vkCtx->GetDevice());
        }
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL)
            m_Pipeline.OnResize(width, height);
    }
    
    void SceneRenderer::BeginScene(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPosition) {
    ResetStats();

    // 记录原始数据
        m_Data->ViewMatrix = viewMatrix;
        m_Data->ProjectionMatrix = projectionMatrix;
        m_Data->CameraPosition = cameraPosition;

        // 1. 应用 Vulkan 深度校正 (保持 Y 轴缩放为 1.0f，配合之前的 Negative Viewport 方案)
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            static const glm::mat4 vulkanCorrection(
                1.0f,  0.0f,  0.0f,  0.0f,
                0.0f, -1.0f,  0.0f,  0.0f,
                0.0f,  0.0f,  0.5f,  0.0f, 
                0.0f,  0.0f,  0.5f,  1.0f  
            );
            m_RenderContext.ProjectionMatrix = vulkanCorrection * projectionMatrix;
        } else {
            m_RenderContext.ProjectionMatrix = projectionMatrix;
        }

        m_RenderContext.ViewMatrix = viewMatrix;
        m_RenderContext.CameraPosition = cameraPosition;

        // ==========================================
        // 2. 【核心修复】：计算 ViewProjection 必须用校正后的 Projection！
        // ==========================================
        m_Data->ViewProjectionMatrix = m_RenderContext.ProjectionMatrix * viewMatrix; 

        // 3. 填充数据结构
        m_Data->CameraData.ViewProjection = m_Data->ViewProjectionMatrix;
        m_Data->CameraData.CameraPosition = m_Data->CameraPosition;
        
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

        m_RenderContext.FrameSteps.clear(); // 【新增】：每帧清空流水账本！
        
        memset(&m_Data->LightData, 0, sizeof(struct_LightData));
        
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
            
            m_Data->LightData.DirLightDir = glm::vec4(dir, 0.0f);
            m_Data->LightData.DirLightColor = glm::vec4(dlc.Color * dlc.Illuminance, 0.0f);
            hasDirLight = true;
            break; 
        }

        if (!hasDirLight) {
            m_Data->LightData.DirLightDir = glm::vec4(0.0f, -1.0f, 0.0f, 0.03f);
        }

        int pointLightIndex = 0;
        auto pointLightGroup = scene->Reg().view<TransformComponent, PointLightComponent>();
        for (auto entityID : pointLightGroup) {
            if (pointLightIndex >= 4) break; 
            auto [transform, plc] = pointLightGroup.get<TransformComponent, PointLightComponent>(entityID);
            
            m_Data->LightData.PointLights[pointLightIndex].Position = glm::vec4(transform.Translation, plc.Radius);
            float candelas = plc.LuminousPower / (4.0f * glm::pi<float>());
            m_Data->LightData.PointLights[pointLightIndex].Color = glm::vec4(plc.Color * candelas, plc.Falloff);
            pointLightIndex++;
        }
        m_Data->LightData.PointLightCount = pointLightIndex;
        m_LightUniformBuffer->SetData(&m_Data->LightData, sizeof(struct_LightData));


        // ==========================================
        // 布置 Render Graph 数据黑板并轰鸣管线！
        // ==========================================
        m_RenderContext.ActiveScene = scene;
        m_RenderContext.Set("ViewportWidth", m_Data->ViewportWidth);
        m_RenderContext.Set("ViewportHeight", m_Data->ViewportHeight);
        m_RenderContext.Set("HasDirLight", hasDirLight);
        m_RenderContext.Set("DirLightDir", glm::vec3(m_Data->LightData.DirLightDir));

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
        m_RenderContext.Set("SelectedEntity", config.SelectedEntity);
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
                break; // 找到全局体积后退出循环
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

        std::shared_ptr<RenderCommandBuffer> cmd = RenderCommandBuffer::Create();

        if (cmd) {
            cmd->Begin();

            if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                uint32_t vpW = m_Data->ViewportWidth;
                uint32_t vpH = m_Data->ViewportHeight;

                BuildRenderGraph(config, vpW, vpH);

                VulkanPipeline::ClearGlobalUBOs();
                auto camUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_CameraUniformBuffer);
                auto lightUBO = std::dynamic_pointer_cast<VulkanUniformBuffer>(m_LightUniformBuffer);
                if (camUBO) for (uint32_t i=0;i<3;i++) VulkanPipeline::SetGlobalUniformBuffer(0,i,camUBO->GetBuffer(i),sizeof(struct_CameraData));
                if (lightUBO) for (uint32_t i=0;i<3;i++) VulkanPipeline::SetGlobalUniformBuffer(1,i,lightUBO->GetBuffer(i),sizeof(struct_LightData));

                m_RenderGraph.Execute(m_RenderContext, *cmd);
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

        // 性能统计
        float totalGPUTime = 0.0f;
        for (const auto& [name, profile] : m_RenderContext.PassProfiles) {
            totalGPUTime += profile.GPUTime;
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

            // 旧纹理放入 3 帧安全期垃圾桶，避免 GPU 仍在读取时被销毁
            if (m_Data->EnvironmentCubemap) {
                DeferredRelease release;
                release.TextureCubes.push_back(m_Data->EnvironmentCubemap);
                release.TextureCubes.push_back(m_Data->IrradianceMap);
                release.TextureCubes.push_back(m_Data->PrefilterMap);
                m_DeferredReleases.push_back(std::move(release));
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
    void SceneRenderer::BuildRenderGraph(const RenderViewConfig& config, uint32_t vpW, uint32_t vpH) {
        if (!m_ViewportDirty) return;
        m_RenderGraph.Clear();

        // 阶段1: 几何 — Shadow + GBuffer
        m_RenderGraph.AddPass("ShadowPass",
            [&](RGBuilder& b) { VulkanShadowPass::DeclareResources(b); },
            [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_ShadowPass) m_ShadowPass->Execute(ctx, c); });
        m_RenderGraph.AddPass("GBufferPass",
            [&](RGBuilder& b) { VulkanGBufferPass::DeclareResources(b, vpW, vpH); },
            [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_GBufferPass) m_GBufferPass->Execute(ctx, c); });

        // 阶段2: 光照HDR — Deferred PBR + IBL + Shadow + Skybox
        m_RenderGraph.AddPass("LightingPass",
            [&](RGBuilder& b) { VulkanLightingPass::DeclareResources(b, vpW, vpH); },
            [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_LightingPass) m_LightingPass->Execute(ctx, c); });

        // 阶段3: ForwardBlend — Skybox LOAD叠加到 Lighting HDR
        m_RenderGraph.AddPass("ForwardBlend",
            [&](RGBuilder& b) { VulkanForwardBlendPass::DeclareResources(b, vpW, vpH); },
            [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_ForwardBlendPass) m_ForwardBlendPass->Execute(ctx, c); });

        // 阶段3.5: Outline — 选中物体白色遮罩 → Selection FBO (PostProcess Sobel 边缘检测)
        m_RenderGraph.AddPass("OutlinePass",
            [&](RGBuilder& b) { VulkanOutlinePass::DeclareResources(b, vpW, vpH); },
            [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_OutlinePass) m_OutlinePass->Execute(ctx, c); });

        // 阶段4: Bloom — 5级 downsample/upsample → "Bloom" (RGBA16F)
        m_RenderGraph.AddPass("BloomPass",
            [&](RGBuilder& b) { VulkanBloomPass::DeclareResources(b, vpW, vpH); },
            [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_BloomPass) m_BloomPass->Execute(ctx, c); });

        // 阶段4: 后处理 — ToneMapping + Bloom合成 + Outline 边缘检测
        m_RenderGraph.AddPass("PostProcessPass",
            [&](RGBuilder& b) { VulkanPostProcessPass::DeclareResources(b, vpW, vpH); },
            [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_PostProcessPass) m_PostProcessPass->Execute(ctx, c); });

        // 阶段5: FXAA — LDR空间抗锯齿 (必须在tone mapping之后)
        m_RenderGraph.AddPass("FXAAPass",
            [&](RGBuilder& b) { VulkanFXAAPass::DeclareResources(b, vpW, vpH); },
            [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_FXAAPass) m_FXAAPass->Execute(ctx, c); });

        // 阶段5: UI — 内置 UI 元素 (独立透明层, bindless)
        m_RenderGraph.AddPass("UIPass",
            [&](RGBuilder& b) { UIPass::DeclareResources(b, vpW, vpH); },
            [&](RenderContext& ctx, RenderCommandBuffer& c) { if (m_UIPass) m_UIPass->Execute(ctx, c); });

        m_FinalExportTexture = "FXAA";

        m_RenderGraph.Compile();
        m_ViewportDirty = false;
    }

    void SceneRenderer::AddCustomPostProcess(std::shared_ptr<CustomPostProcess> pass) {
        m_CustomPostProcesses.push_back(std::move(pass));
    }
    void SceneRenderer::RemoveCustomPostProcess(const std::string& name) {
        // reserved
    }
}