#include "ayapch.h"

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
#include "Renderer/UniformBuffer.hpp"
#include "Renderer/Frustum.hpp"
#include "Renderer/IBLBuilder.hpp"
#include "Asset/AssetManager.hpp"

// 管线 Passes
#include "Renderer/Passes/ShadowPass.hpp"
#include "Renderer/Passes/GBufferPass.hpp"
#include "Renderer/Passes/LightingPass.hpp"
#include "Renderer/Passes/PostProcessPass.hpp"
#include "Renderer/Passes/BloomPass.hpp"
#include "Renderer/Passes/FXAAPass.hpp"

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

    static std::shared_ptr<UniformBuffer> s_CameraUniformBuffer;
    static std::shared_ptr<UniformBuffer> s_LightUniformBuffer;

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
        std::shared_ptr<Mesh> GridMesh;

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
    }
    SceneRenderer::~SceneRenderer() = default;

    void SceneRenderer::Init() {
        m_Data->WhiteTexture = Texture2D::Create(1, 1);
        if (m_Data->WhiteTexture) {
            uint32_t whiteTextureData = 0xffffffff; 
            m_Data->WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));
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
                s_SkyboxShader = Shader::Create("assets/Editor/shaders/Skybox/skybox.vert", "assets/Editor/shaders/Skybox/skybox.frag");
                std::shared_ptr<Shader> brdfShader = Shader::Create("assets/Editor/shaders/IBL/brdf.vert", "assets/Editor/shaders/IBL/brdf.frag");
                if(m_Data->EmptyVAO == 0) glGenVertexArrays(1, &m_Data->EmptyVAO);
                uint32_t brdfID = IBLBuilder::CreateBRDFLUT(brdfShader, m_Data->EmptyVAO);
                s_DefaultBRDFLUT = Texture2D::Create(brdfID, 512, 512);
            } 
            else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                // Vulkan 占位逻辑：防止后续拿到空指针崩溃
                s_DefaultBRDFLUT = Texture2D::Create(1, 1); 
            }
        }

        m_Data->SkyboxMesh = s_SkyboxMesh;
        m_Data->SkyboxShader = s_SkyboxShader;
        m_Data->BRDFLUT = s_DefaultBRDFLUT;
        
        // 全局 UBO 内存分配 (假设 UniformBuffer::Create 已经做了跨平台处理)
        if (!s_CameraUniformBuffer) s_CameraUniformBuffer = UniformBuffer::Create(sizeof(struct_CameraData), 0);
        if (!s_LightUniformBuffer) s_LightUniformBuffer = UniformBuffer::Create(sizeof(struct_LightData), 1);

        // ==========================================
        // 【核心防御 2】：隔离管线和裸露的 OpenGL 查询函数
        // ==========================================
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            m_Pipeline.AddPass(std::make_shared<ShadowPass>());
            m_Pipeline.AddPass(std::make_shared<GBufferPass>());
            m_Pipeline.AddPass(std::make_shared<LightingPass>());
            m_Pipeline.AddPass(std::make_shared<BloomPass>());
            m_Pipeline.AddPass(std::make_shared<PostProcessPass>());
            m_Pipeline.AddPass(std::make_shared<FXAAPass>());
            m_Pipeline.Init();

            // 拦截裸露的 OpenGL 调用！
            if (m_Data->EmptyVAO == 0) glGenVertexArrays(1, &m_Data->EmptyVAO);
            glGenQueries(1, &m_Data->GPUTimeQuery);
        }
        else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            AYAYA_CORE_WARN("Vulkan Pipeline is running in minimal mode!");
            // Vulkan 模式下暂不执行任何 OpenGL 的 VAO 或 Query 创建
        }
    }

    void SceneRenderer::OnWindowResize(uint32_t width, uint32_t height) {
        m_Data->ViewportWidth = width;
        m_Data->ViewportHeight = height;
        m_Pipeline.OnResize(width, height);
    }
    
    void SceneRenderer::BeginScene(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPosition) {
        ResetStats();

        m_Data->ViewMatrix = viewMatrix;
        m_Data->ProjectionMatrix = projectionMatrix;
        m_Data->ViewProjectionMatrix = projectionMatrix * viewMatrix; 
        m_Data->CameraPosition = cameraPosition;
        
        // ==========================================
        // 【核心修复】：将物理矩阵与位置同步给数据黑板，救活整个场景！
        // ==========================================
        m_RenderContext.ViewMatrix = viewMatrix;
        m_RenderContext.ProjectionMatrix = projectionMatrix;
        m_RenderContext.CameraPosition = cameraPosition;

        m_Data->CameraData.ViewProjection = m_Data->ViewProjectionMatrix;
        m_Data->CameraData.CameraPosition = m_Data->CameraPosition;
        s_CameraUniformBuffer->SetData(&m_Data->CameraData, sizeof(struct_CameraData));
    }

    void SceneRenderer::EndScene() {
    }

    void SceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene, Entity hoveredEntity, bool showGrid, bool showSkybox, const glm::vec4& clearColor) {
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
        s_LightUniformBuffer->SetData(&m_Data->LightData, sizeof(struct_LightData));


        // ==========================================
        // 布置 Render Graph 数据黑板并轰鸣管线！
        // ==========================================
        m_RenderContext.ActiveScene = scene;
        m_RenderContext.Set("ViewportWidth", m_Data->ViewportWidth);
        m_RenderContext.Set("ViewportHeight", m_Data->ViewportHeight);
        m_RenderContext.Set("HasDirLight", hasDirLight);
        m_RenderContext.Set("DirLightDir", glm::vec3(m_Data->LightData.DirLightDir));

        m_RenderContext.SetTexture("WhiteTexture", m_Data->WhiteTexture);
        m_RenderContext.Set<std::shared_ptr<Mesh>>("SkyboxMesh", m_Data->SkyboxMesh);
        m_RenderContext.Set<std::shared_ptr<Mesh>>("GridMesh", m_Data->GridMesh);
        m_RenderContext.SetTexture("BRDFLUT", m_Data->BRDFLUT);
        m_RenderContext.Set<std::shared_ptr<TextureCube>>("EnvironmentCubemap", m_Data->EnvironmentCubemap);
        m_RenderContext.Set<std::shared_ptr<TextureCube>>("IrradianceMap", m_Data->IrradianceMap);
        m_RenderContext.Set<std::shared_ptr<TextureCube>>("PrefilterMap", m_Data->PrefilterMap);

        m_RenderContext.Set("ClearColor", clearColor);
        m_RenderContext.Set("ShowSkybox", showSkybox);
        m_RenderContext.Set("ShowGrid", showGrid);
        m_RenderContext.Set("HoveredEntity", hoveredEntity);
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

        // 遍历场景，寻找全局的 Volume (目前暂时只取找到的第一个)
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
            // 将黑板和指令记录器一并传给管线
            m_Pipeline.Execute(m_RenderContext, *cmd);
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

    void* SceneRenderer::GetPostProcessFBORendererID() {
        bool enableFXAA = m_RenderContext.Get<bool>("EnableFXAA", false);
        
        if (enableFXAA && m_RenderContext.Framebuffers.count("FXAA")) {
            return m_RenderContext.Framebuffers["FXAA"]->GetRendererID();
        }
        if (m_RenderContext.Framebuffers.count("PostProcess")) {
            return m_RenderContext.Framebuffers["PostProcess"]->GetRendererID();
        }
        return nullptr; 
    }

    void* SceneRenderer::GetFinalColorAttachmentRendererID() {
        return m_RenderContext.Get<void*>("Final_Output", nullptr);
    }

    void* SceneRenderer::GetBlackboardTextureID(std::string_view key) {
        return m_RenderContext.Get<void*>(key, nullptr);
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

        uint32_t baseCubemapID = 0;

        if (envComp.Type == EnvironmentType::HDR_Equirectangular || envComp.Type == EnvironmentType::LDR_Equirectangular) {
            if (!envComp.EquirectangularTexture) return;
            std::shared_ptr<Shader> convertShader = Shader::Create("assets/Editor/shaders/IBL/equirectangular_to_cubemap.vert", "assets/Editor/shaders/IBL/equirectangular_to_cubemap.frag");
            baseCubemapID = IBLBuilder::ConvertEquirectangularToCubemap(envComp.EquirectangularTexture, s_SkyboxMesh, convertShader);
            m_Data->EnvironmentCubemap = TextureCube::Create(baseCubemapID, 1024, 1024);
        }
        else if (envComp.Type == EnvironmentType::Classic_Cubemap) {
            if (!envComp.ClassicCubemapTexture) return;
            baseCubemapID = envComp.ClassicCubemapTexture->GetRendererID();
            m_Data->EnvironmentCubemap = envComp.ClassicCubemapTexture;
            glBindTexture(GL_TEXTURE_CUBE_MAP, baseCubemapID);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); 
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }

        if (baseCubemapID != 0) {
            std::shared_ptr<Shader> irradianceShader = Shader::Create("assets/Editor/shaders/IBL/cubemap.vert", "assets/Editor/shaders/IBL/irradiance_convolution.frag");
            uint32_t irrID = IBLBuilder::CreateIrradianceMap(baseCubemapID, s_SkyboxMesh, irradianceShader);
            m_Data->IrradianceMap = TextureCube::Create(irrID, 32, 32);

            std::shared_ptr<Shader> prefilterShader = Shader::Create("assets/Editor/shaders/IBL/cubemap.vert", "assets/Editor/shaders/IBL/prefilter.frag");
            uint32_t preID = IBLBuilder::CreatePrefilterMap(baseCubemapID, s_SkyboxMesh, prefilterShader);
            m_Data->PrefilterMap = TextureCube::Create(preID, 128, 128);
            
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
}