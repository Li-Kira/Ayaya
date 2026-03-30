#include "ayapch.h"

// 1. 核心系统 (必须最先包含，确保 entt::entity 等底层类型被提前识别)
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Components.hpp"

// 2. 渲染管线与引擎基础设施
#include "Renderer/SceneRenderer.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/RenderCommand.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/TextureCube.hpp"
#include "Renderer/UniformBuffer.hpp"
#include "Renderer/Frustum.hpp"
#include "Renderer/IBLBuilder.hpp"
#include "Renderer/Passes/FXAAPass.hpp"
#include "Asset/AssetManager.hpp"

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

    struct RenderCommandData {
        glm::mat4 Transform;
        std::shared_ptr<Mesh> MeshAsset;
        std::shared_ptr<Material> MaterialAsset;
        std::shared_ptr<Shader> ShaderAsset;
        Entity TargetEntity;

        // 【新增】：携带阴影状态进入队列
        bool CastShadows;
        bool ReceiveShadows;
    };

    // 使用静态结构体管理管线内部的资源，不对外暴露
    static std::shared_ptr<UniformBuffer> s_CameraUniformBuffer;
    static std::shared_ptr<UniformBuffer> s_LightUniformBuffer;

    // ==========================================
    // 新增：全局静态共享渲染资源 (防止多实例重复加载)
    // ==========================================
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

        // ==========================================
        // 核心升级 1：延迟渲染的专属 Shader 组
        // ==========================================
        std::shared_ptr<Shader> GBufferShader;          // 几何 Pass
        std::shared_ptr<Shader> DeferredLightingShader; // 光照 Pass
        
        std::shared_ptr<Shader> DefaultShader;
        std::shared_ptr<Shader> OutlineShader;
        std::shared_ptr<Shader> GridShader;
        std::shared_ptr<Shader> SpriteShader;

        std::shared_ptr<Shader> FallbackShader;
        std::shared_ptr<Material> FallbackMaterial;

        std::shared_ptr<Mesh> SkyboxMesh;
        std::shared_ptr<Shader> SkyboxShader;
        std::shared_ptr<TextureCube> EnvironmentCubemap; 
        std::shared_ptr<TextureCube> IrradianceMap;
        std::shared_ptr<Mesh> SkyboxCubeMesh;
        std::shared_ptr<TextureCube> PrefilterMap;
        std::shared_ptr<Texture2D>   BRDFLUT;
        float EnvironmentIntensity = 1.0f;
        glm::vec3 EnvironmentAmbientColor = { 0.1f, 0.1f, 0.1f };
        glm::vec4 ClearColor = { 0.06f, 0.06f, 0.065f, 1.0f };

        // 阴影专属资源
        std::shared_ptr<Shader> ShadowShader;
        uint32_t ShadowMapFBO = 0;
        uint32_t ShadowMapTexture = 0;
        glm::mat4 LightSpaceMatrix;

        // 新增 UBO 相关的成员
        struct_CameraData CameraData;
        struct_LightData LightData;

        // ==========================================
        // 核心升级 2：三套 FBO 形成流水线！
        // ==========================================
        std::shared_ptr<Framebuffer> GeometryFBO;    // 装载 4 张 G-Buffer 数据图
        std::shared_ptr<Framebuffer> LightingFBO;    // 装载光照合成结果 (HDR)
        std::shared_ptr<Framebuffer> PostProcessFBO; // 装载最终 LDR 画布
        std::shared_ptr<Framebuffer> SelectionFBO;   // 选中物体的剪影
        std::shared_ptr<Framebuffer> BloomFBO[2];    
    
        std::shared_ptr<Shader> PostProcessShader;   // 后期 Shader
        uint32_t EmptyVAO;                           // 用于全屏绘制的空 VAO

        // 【新增】：Bloom 专属着色器
        std::shared_ptr<Shader> BloomExtractShader;
        std::shared_ptr<Shader> BloomBlurShader;

        // ==========================================
        // 新增：全局渲染队列
        // ==========================================
        std::vector<RenderCommandData> OpaqueDrawList;
        // 新增：静态统计数据
        SceneRenderer::Statistics Stats;
        uint32_t GPUTimeQuery = 0;
    };

    SceneRenderer::SceneRenderer() {
        m_Data = std::make_unique<SceneRendererData>();
    }
    SceneRenderer::~SceneRenderer() = default;

    void SceneRenderer::Init() {
        // 1. 初始化纯白贴图
        m_Data->WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff; 
        m_Data->WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        // 2. 初始化 3D 网格地基
        m_Data->GridMesh = Mesh::CreatePlane(1.0f, 1.0f);

        // 3. 加载管线必需的 Shader
        m_Data->DefaultShader = Shader::Create("assets/Editor/shaders/PBR/pbr.vert", "assets/Editor/shaders/PBR/pbr.frag");
        m_Data->OutlineShader = Shader::Create("assets/Editor/shaders/UI/outline.vert", "assets/Editor/shaders/UI/outline.frag");
        m_Data->GridShader    = Shader::Create("assets/Editor/shaders/UI/grid.vert", "assets/Editor/shaders/UI/grid.frag");
        m_Data->SpriteShader  = Shader::Create("assets/Editor/shaders/2D/sprite.vert", "assets/Editor/shaders/2D/sprite.frag");
        // ==========================================
        // 加载全新的延迟渲染 Shader
        // ==========================================
        m_Data->GBufferShader = Shader::Create("assets/Editor/shaders/Deferred/gbuffer.vert", "assets/Editor/shaders/Deferred/gbuffer.frag");
        m_Data->DeferredLightingShader = Shader::Create("assets/Editor/shaders/Deferred/deferred_lighting.vert", "assets/Editor/shaders/Deferred/deferred_lighting.frag");

        // 4. 加载Fallback材质
        m_Data->FallbackShader = Shader::Create("assets/Editor/shaders/Fallback/fallback.vert", "assets/Editor/shaders/Fallback/fallback.frag");

        // 创建空材质并绑定 Fallback 标识
        m_Data->FallbackMaterial = std::make_shared<Material>();
        m_Data->FallbackMaterial->Name = "Error Fallback";
        m_Data->FallbackMaterial->ShaderName = "Fallback";

        // 5. 加载天空盒
        if (!s_SkyboxMesh) {
            // 只生成必要的基础设施，不加载具体贴图！
            s_SkyboxShader = Shader::Create("assets/Editor/shaders/Skybox/skybox.vert", "assets/Editor/shaders/Skybox/skybox.frag");
            s_SkyboxMesh = Mesh::CreateCube(1.0f);

            // 【保留】：烘焙 BRDF LUT (全局仅需一次)
            std::shared_ptr<Shader> brdfShader = Shader::Create("assets/Editor/shaders/IBL/brdf.vert", "assets/Editor/shaders/IBL/brdf.frag");
            if(m_Data->EmptyVAO == 0) glGenVertexArrays(1, &m_Data->EmptyVAO);
            uint32_t brdfID = IBLBuilder::CreateBRDFLUT(brdfShader, m_Data->EmptyVAO);
            s_DefaultBRDFLUT = Texture2D::Create(brdfID, 512, 512);
        }

        // 把静态共享的基础设施挂载到当前实例
        m_Data->SkyboxMesh = s_SkyboxMesh;
        m_Data->SkyboxShader = s_SkyboxShader;
        m_Data->BRDFLUT = s_DefaultBRDFLUT;
        
        // 注意：EnvironmentCubemap, IrradianceMap, PrefilterMap 现在初始为空！
        m_Data->EnvironmentCubemap = nullptr;
        m_Data->IrradianceMap = nullptr;
        m_Data->PrefilterMap = nullptr;

        // ==========================================
        // 6. 初始化 UBO
        // ==========================================
        if (!s_CameraUniformBuffer) {
            s_CameraUniformBuffer = UniformBuffer::Create(sizeof(struct_CameraData), 0);
        }
        m_Data->GBufferShader->BindUniformBlock("Camera", 0);
        m_Data->DeferredLightingShader->BindUniformBlock("Camera", 0);
        m_Data->DefaultShader->BindUniformBlock("Camera", 0);
        m_Data->OutlineShader->BindUniformBlock("Camera", 0);
        m_Data->GridShader->BindUniformBlock("Camera", 0);
        m_Data->FallbackShader->BindUniformBlock("Camera", 0);
        m_Data->SkyboxShader->BindUniformBlock("Camera", 0);

        // // 新增：初始化 LightData UBO，绑定到 1 号槽位
        // m_Data->LightUniformBuffer = UniformBuffer::Create(sizeof(struct_LightData), 1);
        // m_Data->DefaultShader->BindUniformBlock("LightData", 1);

        if (!s_LightUniformBuffer) {
            s_LightUniformBuffer = UniformBuffer::Create(sizeof(struct_LightData), 1);
        }
        m_Data->DeferredLightingShader->BindUniformBlock("LightData", 1); // 光照数据只交给 LightingShader

        // ==========================================
        // 7. 初始化内部 FBO 和 后期 Pass 资源
        // ==========================================
        // 7.1 G-Buffer FBO (纯数据)
        FramebufferSpecification geoSpec;
        geoSpec.Samples = 1;
        geoSpec.Width = 1280; geoSpec.Height = 720;
        geoSpec.Attachments = { 
            FramebufferTextureFormat::RGBA32F, // Layout 0: 世界坐标 (需要极高精度)
            FramebufferTextureFormat::RGBA16F, // Layout 1: 世界法线 (中等精度)
            FramebufferTextureFormat::RGBA8,   // Layout 2: 漫反射颜色 (Albedo)
            FramebufferTextureFormat::RGBA8,   // Layout 3: 材质参数 (R:金属度, G:粗糙度, B:AO)
            FramebufferTextureFormat::Depth    // 深度缓冲
        };
        m_Data->GeometryFBO = Framebuffer::Create(geoSpec);

        // 7.2 Lighting FBO (合成光照，HDR 精度)
        FramebufferSpecification lightSpec;
        lightSpec.Samples = 1;
        lightSpec.Width = 1280; lightSpec.Height = 720;
        lightSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        m_Data->LightingFBO = Framebuffer::Create(lightSpec);

        // 7.3 PostProcess FBO (屏幕输出，LDR)
        FramebufferSpecification postSpec;
        postSpec.Samples = 1; 
        postSpec.Width = 1280; postSpec.Height = 720;
        postSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_Data->PostProcessFBO = Framebuffer::Create(postSpec);

        m_Data->PostProcessShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/postprocess.frag");

        // 7.4 选中物体描边
        FramebufferSpecification selSpec;
        selSpec.Samples = 1;
        selSpec.Width = 1280; selSpec.Height = 720;
        // 只需要一张简单的颜色贴图和深度贴图即可
        selSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_Data->SelectionFBO = Framebuffer::Create(selSpec);

        // 7.5 Bloom
        // 加载 Shader
        m_Data->BloomExtractShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/bloom_extract.frag");
        m_Data->BloomBlurShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/bloom_blur.frag");
        
        // 创建 Bloom FBO (为了性能和更大的光晕，通常设为屏幕分辨率的 1/2)
        FramebufferSpecification bloomSpec;
        bloomSpec.Samples = 1; 
        bloomSpec.Width = 1280 / 2; bloomSpec.Height = 720 / 2;
        bloomSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth }; // 必须是 16F 兜住 HDR
        m_Data->BloomFBO[0] = Framebuffer::Create(bloomSpec);
        m_Data->BloomFBO[1] = Framebuffer::Create(bloomSpec);

        m_Pipeline.AddPass(std::make_shared<FXAAPass>());
        m_Pipeline.Init();

        // ==========================================
        // 8. 初始化高精度阴影贴图 (2K 分辨率)
        // ==========================================
        m_Data->ShadowShader = Shader::Create("assets/Editor/shaders/Shadow/shadow_map.vert", "assets/Editor/shaders/Shadow/shadow_map.frag");
        
        glGenFramebuffers(1, &m_Data->ShadowMapFBO);
        glGenTextures(1, &m_Data->ShadowMapTexture);
        glBindTexture(GL_TEXTURE_2D, m_Data->ShadowMapTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 2048, 2048, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        // 【核心魔法】：设置边缘颜色为纯白！防止视锥体外围产生多余的死黑阴影
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        glBindFramebuffer(GL_FRAMEBUFFER, m_Data->ShadowMapFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_Data->ShadowMapTexture, 0);
        glDrawBuffer(GL_NONE); // 我们不需要画颜色！
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 9. 其他资源
        // 创建一个空 VAO，供 gl_VertexID 魔法使用
        glGenVertexArrays(1, &m_Data->EmptyVAO);
        // GPU统计
        glGenQueries(1, &m_Data->GPUTimeQuery);
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

        // ==========================================
        // 分支 1：处理单张全景图 (HDR 或 普通 JPG)
        // ==========================================
        if (envComp.Type == EnvironmentType::HDR_Equirectangular || 
            envComp.Type == EnvironmentType::LDR_Equirectangular) 
        {
            if (!envComp.EquirectangularTexture) return;

            // 转换全景图为 Cubemap
            std::shared_ptr<Shader> convertShader = Shader::Create("assets/Editor/shaders/IBL/equirectangular_to_cubemap.vert", "assets/Editor/shaders/IBL/equirectangular_to_cubemap.frag");
            baseCubemapID = IBLBuilder::ConvertEquirectangularToCubemap(envComp.EquirectangularTexture, s_SkyboxMesh, convertShader);
            
            m_Data->EnvironmentCubemap = std::make_shared<TextureCube>(baseCubemapID, 1024, 1024);
        }
        // ==========================================
        // 分支 2：处理传统的 6 面体天空盒
        // ==========================================
        else if (envComp.Type == EnvironmentType::Classic_Cubemap) 
        {
            if (!envComp.ClassicCubemapTexture) return;

            baseCubemapID = envComp.ClassicCubemapTexture->GetRendererID();
            m_Data->EnvironmentCubemap = envComp.ClassicCubemapTexture;

            // 【关键】：传统天空盒默认没生成 Mipmap。为了防止后续 Prefilter 报死黑，必须强制生成！
            glBindTexture(GL_TEXTURE_CUBE_MAP, baseCubemapID);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); 
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }

        // ==========================================
        // 统一烘焙 IBL 光照贴图
        // ==========================================
        if (baseCubemapID != 0) {
            // 烘焙漫反射 Irradiance
            std::shared_ptr<Shader> irradianceShader = Shader::Create("assets/Editor/shaders/IBL/cubemap.vert", "assets/Editor/shaders/IBL/irradiance_convolution.frag");
            uint32_t irrID = IBLBuilder::CreateIrradianceMap(baseCubemapID, s_SkyboxMesh, irradianceShader);
            m_Data->IrradianceMap = std::make_shared<TextureCube>(irrID, 32, 32);

            // 烘焙高光 Prefilter
            std::shared_ptr<Shader> prefilterShader = Shader::Create("assets/Editor/shaders/IBL/cubemap.vert", "assets/Editor/shaders/IBL/prefilter.frag");
            uint32_t preID = IBLBuilder::CreatePrefilterMap(baseCubemapID, s_SkyboxMesh, prefilterShader);
            m_Data->PrefilterMap = std::make_shared<TextureCube>(preID, 128, 128);
            
            // 烘焙完成，清除脏标记
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

    void SceneRenderer::OnWindowResize(uint32_t width, uint32_t height) {
        m_Data->GeometryFBO->Resize(width, height);
        m_Data->LightingFBO->Resize(width, height);
        m_Data->PostProcessFBO->Resize(width, height);
        m_Data->SelectionFBO->Resize(width, height);

        // Bloom 降低一半分辨率
        m_Data->BloomFBO[0]->Resize(width / 2, height / 2);
        m_Data->BloomFBO[1]->Resize(width / 2, height / 2);

        m_Pipeline.OnResize(width, height);
    }
    

    void SceneRenderer::SetMSAASamples(uint32_t samples) {
        // 延迟管线暂不开启硬件 MSAA，我们强制锁死为 1，后期用 FXAA 替代
        auto spec = m_Data->GeometryFBO->GetSpecification();
        // spec.Samples = samples;
        spec.Samples = 1;
        m_Data->GeometryFBO = Framebuffer::Create(spec); // 直接重建
    }

    uint32_t SceneRenderer::GetFinalColorAttachmentRendererID() {
        // 直接从渲染上下文中获取最后一个 Pass 写回来的结果！
        // 如果新管线没干活，兜底返回 PostProcessFBO 的结果。
        return m_RenderContext.Get<uint32_t>("Final_Output", m_Data->PostProcessFBO->GetColorAttachmentRendererID(0));
    }

   void SceneRenderer::BeginScene(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPosition) {
        // 在每帧渲染开始时，清零统计计数器
        ResetStats();

        // 保存分开的矩阵
        m_Data->ViewMatrix = viewMatrix;
        m_Data->ProjectionMatrix = projectionMatrix;
        m_Data->ViewProjectionMatrix = projectionMatrix * viewMatrix; 
        m_Data->CameraPosition = cameraPosition;
        
        // ==========================================
        // 核心：ubo 每帧只在这里把相机数据传给显卡一次！
        // ==========================================
        m_Data->CameraData.ViewProjection = m_Data->ViewProjectionMatrix;
        m_Data->CameraData.CameraPosition = m_Data->CameraPosition;
        s_CameraUniformBuffer->SetData(&m_Data->CameraData, sizeof(struct_CameraData));

        Renderer::BeginScene(m_Data->ViewProjectionMatrix);
    }

    void SceneRenderer::EndScene() {
        Renderer::EndScene();
    }

    void SceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene, Entity hoveredEntity, bool showGrid, bool showSkybox, const glm::vec4& clearColor) {

        // ==========================================
        // 统计CPU和GPU时间
        // ==========================================
        // 1. 记录 CPU 开始时间
        auto cpuStartTime = std::chrono::high_resolution_clock::now();
        // 2. 告诉显卡：开始硬件级时间统计！
        glBeginQuery(GL_TIME_ELAPSED, m_Data->GPUTimeQuery);
        
        // ==========================================
        // Pass 1: Lighting Setup Pass (收集场景灯光)
        // ==========================================
        // 清理旧数据
        memset(&m_Data->LightData, 0, sizeof(struct_LightData));
        
        // 1.1 收集平行光 (直接使用 Lux)
        bool hasDirLight = false;
        auto dirLightGroup = scene->Reg().view<TransformComponent, DirectionalLightComponent>();
        for (auto entityID : dirLightGroup) {
            auto [transform, dlc] = dirLightGroup.get<TransformComponent, DirectionalLightComponent>(entityID);
            glm::quat orientation = glm::quat(transform.Rotation);
            glm::vec3 dir = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
            
            // ==========================================
            // 核心修复：将 UI 上的 AmbientStrength 打包进 w 通道
            // ==========================================
            m_Data->LightData.DirLightDir = glm::vec4(dir, 0.0f);
            m_Data->LightData.DirLightColor = glm::vec4(dlc.Color * dlc.Illuminance, 0.0f);
            hasDirLight = true;
            break; 
        }

        // 如果场景里没有平行光（比如室内棚拍），给一个微弱的默认环境光
        if (!hasDirLight) {
            m_Data->LightData.DirLightDir = glm::vec4(0.0f, -1.0f, 0.0f, 0.03f);
        }

        // 1.2 收集点光源
        int pointLightIndex = 0;
        auto pointLightGroup = scene->Reg().view<TransformComponent, PointLightComponent>();
        for (auto entityID : pointLightGroup) {
            if (pointLightIndex >= 4) break; 
            auto [transform, plc] = pointLightGroup.get<TransformComponent, PointLightComponent>(entityID);
            
            // 【核心修改】：把 Radius 塞进 Position 的 W 通道！
            m_Data->LightData.PointLights[pointLightIndex].Position = glm::vec4(transform.Translation, plc.Radius);
            
            float candelas = plc.LuminousPower / (4.0f * glm::pi<float>());
            
            // 【核心修改】：把 Falloff 塞进 Color 的 W 通道！
            m_Data->LightData.PointLights[pointLightIndex].Color = glm::vec4(plc.Color * candelas, plc.Falloff);
            pointLightIndex++;
        }
        m_Data->LightData.PointLightCount = pointLightIndex;
        s_LightUniformBuffer->SetData(&m_Data->LightData, sizeof(struct_LightData));

        // 1.3: Shadow Map Pass
        if (hasDirLight) {
            // 1. 算出太阳的位置和视角 (正交投影)
            glm::vec3 lightDir = glm::normalize(glm::vec3(m_Data->LightData.DirLightDir));
            glm::vec3 lightPos = -lightDir * 30.0f; // 假装太阳在场景中心上方 30 米处
            
            // 包围盒 40x40 米，深度视距 1.0~100.0 米
            glm::mat4 lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 100.0f);
            glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            m_Data->LightSpaceMatrix = lightProjection * lightView;

            // 2. 绑定阴影专属的 FBO
            m_Data->ShadowShader->Bind();
            m_Data->ShadowShader->SetMat4("u_LightSpaceMatrix", m_Data->LightSpaceMatrix);

            glViewport(0, 0, 2048, 2048); // 强制把分辨率设为阴影图大小！
            glBindFramebuffer(GL_FRAMEBUFFER, m_Data->ShadowMapFBO);
            glClear(GL_DEPTH_BUFFER_BIT); // 只清空深度
            glEnable(GL_DEPTH_TEST);
            // glCullFace(GL_FRONT); // 核心魔法：剔除正面，只画背面，彻底根除漏光和阴影悬浮！
            glCullFace(GL_BACK);

            // 3. 把 OpaqueDrawList 里的所有物体，用最快的方式（没有颜色材质）渲染一遍！
            for (const auto& cmd : m_Data->OpaqueDrawList) {
                // 【核心实现】：如果不允许投影，直接跳过，不画入阴影贴图！
                if (!cmd.CastShadows) continue; 

                m_Data->ShadowShader->SetMat4("u_Transform", cmd.Transform);
                cmd.MeshAsset->GetVertexArray()->Bind();
                glDrawElements(GL_TRIANGLES, cmd.MeshAsset->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
            }

            glCullFace(GL_BACK); // 画完阴影，恢复正常剔除
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            
            // 【极其重要】：把视口尺寸改回 Game 窗口的真实大小，否则接下来的画面全部缩在左下角！
            glViewport(0, 0, m_Data->GeometryFBO->GetSpecification().Width, m_Data->GeometryFBO->GetSpecification().Height);
        }

        // ==========================================
        // Pass 2: Geometry Pass (G-Buffer)
        // ==========================================
        m_Data->GeometryFBO->Bind();
        // 极度关键：坐标贴图的 Alpha 清理为 0，代表天空！
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        RenderCommand::Clear();
        glEnable(GL_DEPTH_TEST);

        m_Data->OpaqueDrawList.clear();
        // 根据当前相机的 ViewProjection 矩阵生成视锥体
        Frustum cameraFrustum(m_Data->ViewProjectionMatrix);

        // ------------------------------------------
        // 阶段 2.1：收集与剔除 (Collection & Culling)
        // ------------------------------------------
        int totalMeshes = 0;
        int drawnMeshes = 0;

        auto meshGroup = scene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshGroup) {
            Entity entity{ entityID, scene.get() };
            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            glm::mat4 transform = entity.GetWorldTransform();

            // 如果没有模型资产或者没有激活，直接跳过
            if (!meshComp.ModelAsset || !entity.IsActiveInHierarchy()) continue;

            // 相机剔除
            bool isVisible = false;
            for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                if (cameraFrustum.IsBoxVisible(mesh->GetAABB(), transform)) { isVisible = true; break; }
            }
            if (!isVisible) continue;

            // 判断使用的 Shader 和 Material
            bool isFallback = (!meshComp.MaterialAsset || meshComp.MaterialAsset->Properties.empty());
            std::shared_ptr<Shader> targetShader = isFallback ? m_Data->FallbackShader : m_Data->GBufferShader;
            std::shared_ptr<Material> targetMaterial = isFallback ? nullptr : meshComp.MaterialAsset;

           for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                RenderCommandData cmd = { 
                    transform, mesh, targetMaterial, targetShader, entity, 
                    meshComp.CastShadows, meshComp.ReceiveShadows 
                };
                m_Data->OpaqueDrawList.push_back(cmd);
            }
        }

        // ------------------------------------------
        // 阶段 2.2：状态排序 (State Sorting)
        // ------------------------------------------
        // 排序优先级：Shader -> Material -> Mesh
        // 我们直接比较智能指针的底层内存地址 (.get())，这是极其高效的 O(1) 比较！
       std::sort(m_Data->OpaqueDrawList.begin(), m_Data->OpaqueDrawList.end(), [](const auto& a, const auto& b) {
            if (a.ShaderAsset.get() != b.ShaderAsset.get()) return a.ShaderAsset.get() < b.ShaderAsset.get();
            if (a.MaterialAsset.get() != b.MaterialAsset.get()) return a.MaterialAsset.get() < b.MaterialAsset.get();
            return a.MeshAsset.get() < b.MeshAsset.get();
        });

        // ------------------------------------------
        // 阶段 2.3：批量执行 (Execution)
        // ------------------------------------------
        // 用于记录当前显卡的状态，避免重复切换
        std::shared_ptr<Shader> currentShader = nullptr;
        std::shared_ptr<Material> currentMaterial = nullptr;

        for (const auto& cmd : m_Data->OpaqueDrawList) {
            // 1. 只有当 Shader 发生变化时，才调用昂贵的 Bind()
            if (currentShader != cmd.ShaderAsset) {
                currentShader = cmd.ShaderAsset;
                currentShader->Bind();
                m_Data->Stats.ShaderBinds++;
            }

            // 2. 只有当 Material 发生变化时，才重新上传 Uniform 和绑定贴图
            if (currentMaterial != cmd.MaterialAsset) {
                currentMaterial = cmd.MaterialAsset;
                
                if (currentMaterial) {
                    int textureSlot = 0; 
                    for (auto& prop : currentMaterial->Properties) {
                        switch (prop.Type) {
                            case MaterialPropertyType::Float: currentShader->SetFloat(prop.UniformName, prop.FloatValue); break;
                            case MaterialPropertyType::Int:   currentShader->SetInt(prop.UniformName, prop.IntValue); break;
                            case MaterialPropertyType::Bool:  currentShader->SetBool(prop.UniformName, prop.BoolValue); break;
                            case MaterialPropertyType::Vec2:  currentShader->SetFloat2(prop.UniformName, prop.Vec2Value); break;
                            case MaterialPropertyType::Vec3:  currentShader->SetFloat3(prop.UniformName, prop.Vec3Value); break;
                            case MaterialPropertyType::Vec4:  currentShader->SetFloat4(prop.UniformName, prop.Vec4Value); break;
                            case MaterialPropertyType::Texture2D:
                                currentShader->SetInt(prop.UniformName, textureSlot);
                                if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                    AssetManager::GetAsset<Texture2D>(prop.TextureHandle)->Bind(textureSlot);
                                } else {
                                    m_Data->WhiteTexture->Bind(textureSlot); 
                                }
                                textureSlot++;
                                break;
                            default: break;
                        }
                    }
                }
            }

            // 【核心实现】：向 G-Buffer Shader 实时传递当前物体是否接收阴影的标记
            currentShader->SetFloat("u_ReceiveShadows", cmd.ReceiveShadows ? 1.0f : 0.0f);

            // 3. 提交绘制！(Renderer 会负责绑定 VAO 和上传 Transform 矩阵)
            Renderer::Submit(currentShader, cmd.MeshAsset->GetVertexArray(), cmd.Transform);

            // 记录实体模型的绘制
            m_Data->Stats.DrawCalls++;
            m_Data->Stats.VertexCount += cmd.MeshAsset->GetVertexCount();
            m_Data->Stats.TriangleCount += cmd.MeshAsset->GetIndexCount() / 3;
        }

        // 剔除日志
        // AYAYA_CORE_TRACE("Culling: {0} / {1} meshes rendered", drawnMeshes, totalMeshes);
        m_Data->GeometryFBO->Unbind();

        // ==========================================
        // Pass 3: Deferred Lighting Pass (光照核爆合成)
        // ==========================================
        m_Data->LightingFBO->Bind();
        // 用相机的实际背景色清屏！
        // ==========================================
        // 【核心修复 2】：为背景色注入物理能量！
        // 1. 获取当前相机的物理曝光系数
        // ==========================================
        float currentEV100 = 14.5f;
        auto cameraView = scene->Reg().view<CameraComponent>();
        for (auto entityID : cameraView) {
            auto& cc = cameraView.get<CameraComponent>(entityID);
            if (cc.Primary) { currentEV100 = cc.EV100; break; }
        }
        float physicalExposure = 1.0f / (1.2f * std::exp2(currentEV100));

        // ==========================================
        // 2. 将 UI 上选的 LDR 普通颜色 (0~1)，除以曝光衰减系数
        //    将其强行放大到几万级别的 HDR 能量，从而抵消后期的曝光变暗！
        // ==========================================
        glm::vec4 hdrClearColor = m_Data->ClearColor;
        hdrClearColor.r /= physicalExposure;
        hdrClearColor.g /= physicalExposure;
        hdrClearColor.b /= physicalExposure;

        RenderCommand::SetClearColor(hdrClearColor);
        RenderCommand::Clear();

        // 画全屏四边形，绝对不能开启深度测试！
        glDisable(GL_DEPTH_TEST);
        m_Data->DeferredLightingShader->Bind();

        // 将 G-Buffer 塞进插槽
        m_Data->DeferredLightingShader->SetInt("g_Position", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_Data->GeometryFBO->GetColorAttachmentRendererID(0));

        m_Data->DeferredLightingShader->SetInt("g_Normal", 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_Data->GeometryFBO->GetColorAttachmentRendererID(1));

        m_Data->DeferredLightingShader->SetInt("g_Albedo", 2);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_Data->GeometryFBO->GetColorAttachmentRendererID(2));

        m_Data->DeferredLightingShader->SetInt("g_PBR", 3);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_Data->GeometryFBO->GetColorAttachmentRendererID(3));

        // 【新增】：绑定 IBL 漫反射贴图 (使用空闲的槽位，比如 4)
        bool envMapEnabled = false;
        if (m_Data->IrradianceMap && m_Data->PrefilterMap) {
            envMapEnabled = true;
            m_Data->IrradianceMap->Bind(4); 
            m_Data->DeferredLightingShader->SetInt("u_IrradianceMap", 4);
            
            m_Data->PrefilterMap->Bind(5); 
            m_Data->DeferredLightingShader->SetInt("u_PrefilteredMap", 5);
            
            m_Data->DeferredLightingShader->SetFloat("u_Intensity", m_Data->EnvironmentIntensity); 
        } 
        
        // 传递开关状态，告诉显卡是否要采样天空盒
        m_Data->DeferredLightingShader->SetBool("u_EnvMapEnabled", envMapEnabled);
        
        // 彻底解绑：不再乘以 EnvironmentIntensity，独立传递！
        m_Data->DeferredLightingShader->SetFloat3("u_AmbientColor", m_Data->EnvironmentAmbientColor * m_Data->EnvironmentIntensity);

        if (m_Data->BRDFLUT) {
            m_Data->BRDFLUT->Bind(6); 
            m_Data->DeferredLightingShader->SetInt("u_BRDFLUT", 6);
        }

        if (hasDirLight) {
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, m_Data->ShadowMapTexture);
            m_Data->DeferredLightingShader->SetInt("u_ShadowMap", 7);
            m_Data->DeferredLightingShader->SetMat4("u_LightSpaceMatrix", m_Data->LightSpaceMatrix);
        }

        glBindVertexArray(m_Data->EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        
        m_Data->Stats.DrawCalls++;
        m_Data->Stats.VertexCount += 3;
        
        glEnable(GL_DEPTH_TEST); // 画完恢复深度测试
        // 注意：不解绑，我们要继续在这个 FBO 上画前方物理世界的其余东西！

        // ==========================================
        // Pass 4: Depth
        // ==========================================
        // 我们把 G-Buffer 里的深度图复制过来，这样等会画网格和天空盒时才知道怎么遮挡！
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Data->GeometryFBO->GetRendererID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_Data->LightingFBO->GetRendererID());
        
        uint32_t width = m_Data->GeometryFBO->GetSpecification().Width;
        uint32_t height = m_Data->GeometryFBO->GetSpecification().Height;
        glBlitFramebuffer(0, 0, width, height,
                          0, 0, width, height,
                          GL_DEPTH_BUFFER_BIT,
                          GL_NEAREST);
        // 重新确立 Lighting FBO 的绘制焦点
        glBindFramebuffer(GL_FRAMEBUFFER, m_Data->LightingFBO->GetRendererID());

        // ==========================================
        // Pass 5: Forward Pass (天空盒、网格、描边)
        // ==========================================
        
        // ------------------------------------------
        // Pass 5.1: 天空盒
        // ------------------------------------------
        if (showSkybox && m_Data->EnvironmentCubemap) {
            glDepthFunc(GL_LEQUAL);  
            m_Data->SkyboxShader->Bind();
            // ==========================================
            // 【新增】：向天空盒注入物理能量倍增器
            // ==========================================
            // 因为当前相机的 EV100 高达 14.5，我们需要极高的亮度才能被看见。
            // 这里暂定 30000.0f，后续可移入 Scene 的 Environment 属性中由用户调节。
            m_Data->SkyboxShader->SetFloat("u_Intensity", m_Data->EnvironmentIntensity);

            glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(m_Data->ViewMatrix));
            m_Data->SkyboxShader->SetMat4("u_View", viewNoTranslation);
            
            glm::mat4 skyboxProjection = m_Data->ProjectionMatrix;
            if (m_Data->ProjectionMatrix[3][3] == 1.0f) {
                float aspect = (float)m_Data->GeometryFBO->GetSpecification().Width / (float)m_Data->GeometryFBO->GetSpecification().Height;
                skyboxProjection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
            }
            m_Data->SkyboxShader->SetMat4("u_Projection", skyboxProjection);
            
            if (m_Data->EnvironmentCubemap) {
                m_Data->EnvironmentCubemap->Bind(0); 
                m_Data->SkyboxShader->SetInt("u_Skybox", 0);
            }

            glDisable(GL_CULL_FACE); 
            Renderer::Submit(m_Data->SkyboxShader, m_Data->SkyboxMesh->GetVertexArray(), glm::mat4(1.0f));
            glEnable(GL_CULL_FACE);
            glDepthFunc(GL_LESS);
            
            m_Data->Stats.DrawCalls++;
            m_Data->Stats.VertexCount += 36; 
        }

        // ------------------------------------------
        // Pass 5.2: 网格
        // ------------------------------------------
        if (showGrid) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthFunc(GL_LESS);  
            glDepthMask(GL_FALSE); 
            glDisable(GL_CULL_FACE);
            
            m_Data->GridShader->Bind();
            m_Data->GridShader->SetFloat("u_ExposureInverse", 1.0f / physicalExposure);
            
            glm::mat4 gridTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1000.0f, 1.0f, 1000.0f));
            Renderer::Submit(m_Data->GridShader, m_Data->GridMesh->GetVertexArray(), gridTransform);
            
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE); 
            glDisable(GL_BLEND); 
        }

        // ------------------------------------------
        // Pass 5.3:【新增】：渲染 2D Sprites (支持透明度与画家算法排序)
        // ------------------------------------------
        {
            // 开启混合模式，处理 PNG 的透明度 (Alpha Blending)
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            // 极度关键：关闭深度写入！防止透明物体的黑色边缘互相遮挡
            glDepthMask(GL_FALSE); 
            // 极度关键：关闭背面剔除！纸片人应该是双面可见的
            glDisable(GL_CULL_FACE); 

            m_Data->SpriteShader->Bind();

            // 1. 定义一个收集指令的结构体
            struct SpriteDrawCommand {
                glm::mat4 Transform;
                SpriteRendererComponent SpriteComp;
                float DistanceToCamera;
            };
            std::vector<SpriteDrawCommand> spriteDrawList;

            // 2. 遍历场景收集所有的 Sprite
            auto spriteGroup = scene->Reg().view<TransformComponent, SpriteRendererComponent>();
            for (auto entityID : spriteGroup) {
                Entity entity{ entityID, scene.get() };
                if (!entity.IsActiveInHierarchy()) continue; // 剔除被隐藏的物体

                auto [transformComp, spriteComp] = spriteGroup.get<TransformComponent, SpriteRendererComponent>(entityID);
                glm::mat4 transform = entity.GetWorldTransform();
                
                // 计算该 Sprite 距离相机的距离，为排序做准备
                float distance = glm::length(m_Data->CameraPosition - glm::vec3(transform[3]));
                spriteDrawList.push_back({ transform, spriteComp, distance });
            }

            // 3. 核心排序 (Painter's Algorithm)：由远及近绘制！(距离大的排在前面)
            std::sort(spriteDrawList.begin(), spriteDrawList.end(), [](const SpriteDrawCommand& a, const SpriteDrawCommand& b) {
                return a.DistanceToCamera > b.DistanceToCamera; 
            });

            // 4. 批量执行绘制 (利用 EmptyVAO 和 gl_VertexID 魔法，无需任何 Mesh！)
            glBindVertexArray(m_Data->EmptyVAO); 
            
            for (const auto& cmd : spriteDrawList) {
                m_Data->SpriteShader->SetMat4("u_Transform", cmd.Transform);
                m_Data->SpriteShader->SetFloat4("u_Color", cmd.SpriteComp.Color);

                // 判断是否挂载了贴图
                if (cmd.SpriteComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(cmd.SpriteComp.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(cmd.SpriteComp.TextureHandle);
                    tex->Bind(0);
                    m_Data->SpriteShader->SetInt("u_Texture", 0);
                    m_Data->SpriteShader->SetBool("u_UseTexture", true);
                } else {
                    m_Data->WhiteTexture->Bind(0);
                    m_Data->SpriteShader->SetInt("u_Texture", 0);
                    m_Data->SpriteShader->SetBool("u_UseTexture", false);
                }

                // physicalExposure 是你在 Pass 3 算好的局部变量，直接拿来用！
                m_Data->SpriteShader->SetFloat("u_ExposureInverse", 1.0f / physicalExposure);

                // 核心魔法：绘制 4 个顶点，生成 Triangle Strip！
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                m_Data->Stats.DrawCalls++;
                m_Data->Stats.TriangleCount += 2;
                m_Data->Stats.VertexCount += 4;
            }

            // 恢复渲染器的初始状态，防止污染后续管线
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        m_Data->SelectionFBO->Bind();
        // 清空为纯透明黑色
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        RenderCommand::Clear();

        // ------------------------------------------
        // Pass 5.4:鼠标拾取物体描边，包括Mesh 和 Sprite
        // ------------------------------------------
        if (hoveredEntity && hoveredEntity.IsActiveInHierarchy()) {
            
            // 关闭深度测试，实现穿透墙壁的 X-Ray 选择效果！
            glDisable(GL_DEPTH_TEST); 

            glm::mat4 transform = hoveredEntity.GetWorldTransform();

            // ==========================================
            // 分支 1：处理 3D 模型的描边剪影
            // ==========================================
            if (hoveredEntity.HasComponent<MeshRendererComponent>()) {
                m_Data->OutlineShader->Bind();
                
                // 直接输出纯白色剪影，不再需要算什么物理曝光！
                m_Data->OutlineShader->SetFloat3("u_Color", glm::vec3(1.0f, 1.0f, 1.0f)); 

                auto& meshComp = hoveredEntity.GetComponent<MeshRendererComponent>();
                if (meshComp.ModelAsset) {
                    for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                        Renderer::Submit(m_Data->OutlineShader, mesh->GetVertexArray(), transform);
                    }
                }
            }
            // ==========================================
            // 分支 2：处理 2D 精灵的像素级描边剪影
            // ==========================================
            else if (hoveredEntity.HasComponent<SpriteRendererComponent>()) {
                auto& spriteComp = hoveredEntity.GetComponent<SpriteRendererComponent>();

                m_Data->SpriteShader->Bind();
                m_Data->SpriteShader->SetMat4("u_Transform", transform);
                
                // 强行覆盖为纯白颜色，制造剪影
                m_Data->SpriteShader->SetFloat4("u_Color", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
                // 剪影图不需要曝光衰减，维持 1.0 即可
                m_Data->SpriteShader->SetFloat("u_ExposureInverse", 1.0f);

                // 【绝妙核心】：必须绑定贴图，这样 fragment shader 里的 discard 逻辑 
                // 才能把透明背景剔除，从而得到像素完美的角色边缘！
                if (spriteComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(spriteComp.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(spriteComp.TextureHandle);
                    tex->Bind(0);
                    m_Data->SpriteShader->SetInt("u_Texture", 0);
                    m_Data->SpriteShader->SetBool("u_UseTexture", true);
                } else {
                    m_Data->WhiteTexture->Bind(0);
                    m_Data->SpriteShader->SetInt("u_Texture", 0);
                    m_Data->SpriteShader->SetBool("u_UseTexture", false);
                }

                glBindVertexArray(m_Data->EmptyVAO);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
        }
        glEnable(GL_DEPTH_TEST);
        m_Data->SelectionFBO->Unbind();
        m_Data->LightingFBO->Unbind();

        // ------------------------------------------
        // 【新增】Pass 5.5: Bloom 泛光系统
        // ------------------------------------------
        bool bloomActive = m_EnableBloom;
        if (bloomActive) {
            // 阶段 1：高光提取 (提取 LightingFBO 的内容到 BloomFBO[0])
            m_Data->BloomFBO[0]->Bind();
            RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
            RenderCommand::Clear();
            glDisable(GL_DEPTH_TEST);
            
            m_Data->BloomExtractShader->Bind();
            m_Data->BloomExtractShader->SetInt("u_ScreenTexture", 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_Data->LightingFBO->GetColorAttachmentRendererID(0));
            
            // 传入 UI 设置的阈值，并且附带物理相机的曝光系数进行校准
            m_Data->BloomExtractShader->SetFloat("u_Threshold", m_BloomThreshold);
            m_Data->BloomExtractShader->SetFloat("u_Exposure", physicalExposure * m_Exposure);
            
            glBindVertexArray(m_Data->EmptyVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            
            // 阶段 2：乒乓高斯模糊 (Ping-Pong Blur)
            bool horizontal = true, first_iteration = true;
            int amount = 10; // 模糊次数，数值越大光晕越扩散 (必须是偶数)
            m_Data->BloomBlurShader->Bind();
            m_Data->BloomBlurShader->SetInt("u_Image", 0);
            
            for (int i = 0; i < amount; i++) {
                m_Data->BloomFBO[horizontal]->Bind();
                m_Data->BloomBlurShader->SetBool("u_Horizontal", horizontal);
                
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, first_iteration ? m_Data->BloomFBO[0]->GetColorAttachmentRendererID(0) : m_Data->BloomFBO[!horizontal]->GetColorAttachmentRendererID(0));
                
                glDrawArrays(GL_TRIANGLES, 0, 3);
                horizontal = !horizontal;
                if (first_iteration) first_iteration = false;
            }
            m_Data->BloomFBO[0]->Unbind();
        }
        
        // ==========================================
        // Pass 6: Post-Processing Pass
        // ==========================================
        m_Data->PostProcessFBO->Bind();
        // PostProcessFBO 只需要清空颜色即可，不需要清空深度
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        RenderCommand::Clear();

        glDisable(GL_DEPTH_TEST); 
        m_Data->PostProcessShader->Bind();
        m_Data->PostProcessShader->SetInt("u_ScreenTexture", 0);

        // ==========================================
        // ToneMapping
        // ==========================================
        float finalExposure = physicalExposure * m_Exposure;
        m_Data->PostProcessShader->SetFloat("u_Exposure", finalExposure);
        m_Data->PostProcessShader->SetInt("u_ToneMappingType", m_ToneMappingType);
        m_Data->PostProcessShader->SetInt("u_SelectionTexture", 1);

        // ==========================================
        // Bloom
        // ==========================================
        m_Data->PostProcessShader->SetBool("u_EnableBloom", bloomActive);
        if (bloomActive) {
            m_Data->PostProcessShader->SetFloat("u_BloomIntensity", m_BloomIntensity);
            m_Data->PostProcessShader->SetInt("u_BloomTexture", 2); // 让 Bloom 贴图占用 2 号槽位
        }

        // 绑定 0 号槽位：Lighting FBO 的 HDR 输出
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_Data->LightingFBO->GetColorAttachmentRendererID(0));

        // 绑定 1 号槽位：刚刚画好的剪影图
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_Data->SelectionFBO->GetColorAttachmentRendererID(0));

        // ==========================================
        // 绑定 2 号槽位：把刚才乒乓模糊好的泛光图塞进去！
        // ==========================================
        if (bloomActive) {
            glActiveTexture(GL_TEXTURE2);
            // 因为 amount 是偶数 (10)，所以结果一定回到了 BloomFBO[0]
            glBindTexture(GL_TEXTURE_2D, m_Data->BloomFBO[0]->GetColorAttachmentRendererID(0));
        }

        glBindVertexArray(m_Data->EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        
        m_Data->PostProcessFBO->Unbind();

        // ==========================================
        // 7. 【架构升级】：通过新管线执行后处理 (目前只有 FXAA)
        // ==========================================
        // 1. 填充数据黑板
        m_RenderContext.ActiveScene = scene;
        m_RenderContext.Set("EnableFXAA", m_EnableFXAA);
        // 将旧管线算完的 PostProcessFBO 贴图 ID 挂到黑板上，交接给新管线
        m_RenderContext.Set("PostProcess_Output", m_Data->PostProcessFBO->GetColorAttachmentRendererID(0));

        // 2. 一键轰鸣，执行管线里注册的所有 Pass！
        m_Pipeline.Execute(m_RenderContext);

        // ==========================================
        // 统计CPU和GPU时间
        // ==========================================
        // 3. 告诉显卡：渲染结束，停止统计！
        glEndQuery(GL_TIME_ELAPSED);
        // 4. 从显卡硬件中取回耗时 (纳秒转毫秒)
        // 注意：这会导致 CPU 稍微等待一下显卡 (Pipeline Stall)，在编辑器 UI 中完全可以接受
        uint64_t gpuTimeNs = 0;
        glGetQueryObjectui64v(m_Data->GPUTimeQuery, GL_QUERY_RESULT, &gpuTimeNs);
        m_Data->Stats.GPUTime = (float)gpuTimeNs / 1000000.0f; 
        // 5. 记录 CPU 结束时间并计算耗时
        auto cpuEndTime = std::chrono::high_resolution_clock::now();
        m_Data->Stats.CPUTime = std::chrono::duration<float, std::milli>(cpuEndTime - cpuStartTime).count();
    }

    uint32_t SceneRenderer::GetPostProcessFBORendererID() {
        return m_Data->PostProcessFBO->GetRendererID();
    }

    void SceneRenderer::ResetStats() {
        memset(&m_Data->Stats, 0, sizeof(Statistics));
    }

    SceneRenderer::Statistics SceneRenderer::GetStats() {
        return m_Data->Stats;
    }
}