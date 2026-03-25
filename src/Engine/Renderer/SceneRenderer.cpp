#include "ayapch.h"
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
#include "Asset/AssetManager.hpp"
#include "Engine/Scene/Components.hpp"


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

        std::shared_ptr<Shader> FallbackShader;
        std::shared_ptr<Material> FallbackMaterial;

        std::shared_ptr<Mesh> SkyboxMesh;
        std::shared_ptr<Shader> SkyboxShader;
        std::shared_ptr<TextureCube> EnvironmentCubemap; 
        std::shared_ptr<TextureCube> IrradianceMap;
        std::shared_ptr<Mesh> SkyboxCubeMesh;
        std::shared_ptr<TextureCube> PrefilterMap;
        std::shared_ptr<Texture2D>   BRDFLUT;

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
        
        std::shared_ptr<Shader> PostProcessShader;   // 后期 Shader
        uint32_t EmptyVAO;                           // 用于全屏绘制的空 VAO

        // ==========================================
        // 新增：全局渲染队列
        // ==========================================
        std::vector<RenderCommandData> OpaqueDrawList;
        // 新增：静态统计数据
        SceneRenderer::Statistics Stats;
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
        // m_Data->SkyboxShader = Shader::Create("assets/Editor/shaders/Skybox/skybox.vert", "assets/Editor/shaders/Skybox/skybox.frag");
        // m_Data->SkyboxMesh = Mesh::CreateCube(1.0f);
        // 加载天空盒纹理 (按 Right, Left, Top, Bottom, Front, Back 顺序)
        // std::vector<std::string> faces = {
        //     "assets/textures/skybox/right.jpg",
        //     "assets/textures/skybox/left.jpg",
        //     "assets/textures/skybox/top.jpg",
        //     "assets/textures/skybox/bottom.jpg",
        //     "assets/textures/skybox/front.jpg",
        //     "assets/textures/skybox/back.jpg"
        // };
        // m_Data->EnvironmentCubemap = std::make_shared<TextureCube>(faces);

        if (!s_SkyboxMesh) {
            // 只有第一个 Renderer 初始化时，才会执行这段极度耗时的烘焙代码
            s_SkyboxShader = Shader::Create("assets/Editor/shaders/Skybox/skybox.vert", "assets/Editor/shaders/Skybox/skybox.frag");
            s_SkyboxMesh = Mesh::CreateCube(1.0f);

            std::shared_ptr<Texture2D> hdrTexture = Texture2D::Create("assets/textures/skybox/hdr/newport_loft.hdr");
            std::shared_ptr<Shader> convertShader = Shader::Create("assets/Editor/shaders/IBL/equirectangular_to_cubemap.vert", "assets/Editor/shaders/IBL/equirectangular_to_cubemap.frag");
            
            uint32_t envCubemapID = IBLBuilder::ConvertEquirectangularToCubemap(hdrTexture, s_SkyboxMesh, convertShader);
            s_DefaultEnvironmentMap = std::make_shared<TextureCube>(envCubemapID, 1024, 1024);

            std::shared_ptr<Shader> irradianceShader = Shader::Create("assets/Editor/shaders/IBL/cubemap.vert", "assets/Editor/shaders/IBL/irradiance_convolution.frag");
            uint32_t irradianceMapID = IBLBuilder::CreateIrradianceMap(envCubemapID, s_SkyboxMesh, irradianceShader);
            s_DefaultIrradianceMap = std::make_shared<TextureCube>(irradianceMapID, 32, 32);
            // 【新增】：烘焙 Prefilter
            std::shared_ptr<Shader> prefilterShader = Shader::Create("assets/Editor/shaders/IBL/cubemap.vert", "assets/Editor/shaders/IBL/prefilter.frag");
            uint32_t prefilterID = IBLBuilder::CreatePrefilterMap(envCubemapID, s_SkyboxMesh, prefilterShader);
            s_DefaultPrefilterMap = std::make_shared<TextureCube>(prefilterID, 128, 128);

            // 【新增】：烘焙 BRDF LUT
            std::shared_ptr<Shader> brdfShader = Shader::Create("assets/Editor/shaders/IBL/brdf.vert", "assets/Editor/shaders/IBL/brdf.frag");
            // 注意：把在底下 7.4 创建的 m_Data->EmptyVAO 挪到前面来，因为这里要用！
            if(m_Data->EmptyVAO == 0) glGenVertexArrays(1, &m_Data->EmptyVAO);
            uint32_t brdfID = IBLBuilder::CreateBRDFLUT(brdfShader, m_Data->EmptyVAO);
            s_DefaultBRDFLUT = Texture2D::Create(brdfID, 512, 512);
        }

        // 把静态共享的资源挂载到当前的 Renderer 实例身上，供渲染时使用
        m_Data->SkyboxMesh = s_SkyboxMesh;
        m_Data->SkyboxShader = s_SkyboxShader;
        m_Data->EnvironmentCubemap = s_DefaultEnvironmentMap;
        m_Data->IrradianceMap = s_DefaultIrradianceMap;
        m_Data->PrefilterMap = s_DefaultPrefilterMap;
        m_Data->BRDFLUT = s_DefaultBRDFLUT;

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

        // 创建一个空 VAO，供 gl_VertexID 魔法使用
        glGenVertexArrays(1, &m_Data->EmptyVAO);
    }

    void SceneRenderer::OnWindowResize(uint32_t width, uint32_t height) {
        m_Data->GeometryFBO->Resize(width, height);
        m_Data->LightingFBO->Resize(width, height);
        m_Data->PostProcessFBO->Resize(width, height);
        m_Data->SelectionFBO->Resize(width, height);
    }

    void SceneRenderer::SetMSAASamples(uint32_t samples) {
        // 延迟管线暂不开启硬件 MSAA，我们强制锁死为 1，后期用 FXAA 替代
        auto spec = m_Data->GeometryFBO->GetSpecification();
        // spec.Samples = samples;
        spec.Samples = 1;
        m_Data->GeometryFBO = Framebuffer::Create(spec); // 直接重建
    }

    uint32_t SceneRenderer::GetFinalColorAttachmentRendererID() {
        return m_Data->PostProcessFBO->GetColorAttachmentRendererID();
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
        // Pass 1: Lighting Setup Pass (收集场景灯光)
        // ==========================================
        // 清理旧数据
        memset(&m_Data->LightData, 0, sizeof(struct_LightData));
        
        // 1. 收集平行光 (直接使用 Lux)
        bool hasDirLight = false;
        auto dirLightGroup = scene->Reg().view<TransformComponent, DirectionalLightComponent>();
        for (auto entityID : dirLightGroup) {
            auto [transform, dlc] = dirLightGroup.get<TransformComponent, DirectionalLightComponent>(entityID);
            glm::quat orientation = glm::quat(transform.Rotation);
            glm::vec3 dir = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
            
            // ==========================================
            // 核心修复：将 UI 上的 AmbientStrength 打包进 w 通道
            // ==========================================
            m_Data->LightData.DirLightDir = glm::vec4(dir, dlc.AmbientStrength);
            m_Data->LightData.DirLightColor = glm::vec4(dlc.Color * dlc.Illuminance, 0.0f);
            hasDirLight = true;
            break; 
        }

        // 如果场景里没有平行光（比如室内棚拍），给一个微弱的默认环境光
        if (!hasDirLight) {
            m_Data->LightData.DirLightDir = glm::vec4(0.0f, -1.0f, 0.0f, 0.03f);
        }

        // 2. 收集点光源
        int pointLightIndex = 0;
        auto pointLightGroup = scene->Reg().view<TransformComponent, PointLightComponent>();
        for (auto entityID : pointLightGroup) {
            if (pointLightIndex >= 4) break; 
            auto [transform, plc] = pointLightGroup.get<TransformComponent, PointLightComponent>(entityID);
            m_Data->LightData.PointLights[pointLightIndex].Position = glm::vec4(transform.Translation, 1.0f);
            
            // 物理换算：流明 (Lumens) 转换为 坎德拉 (Candelas)，供 shader 做平方反比衰减
            float candelas = plc.LuminousPower / (4.0f * glm::pi<float>());
            m_Data->LightData.PointLights[pointLightIndex].Color = glm::vec4(plc.Color * candelas, 1.0f);
            pointLightIndex++;
        }
        m_Data->LightData.PointLightCount = pointLightIndex;
        s_LightUniformBuffer->SetData(&m_Data->LightData, sizeof(struct_LightData));


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
                RenderCommandData cmd = { transform, mesh, targetMaterial, targetShader, entity };
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
        RenderCommand::SetClearColor(clearColor);
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
        if (m_Data->IrradianceMap) {
            m_Data->IrradianceMap->Bind(4); 
            m_Data->DeferredLightingShader->SetInt("u_IrradianceMap", 4);
            // 与天空盒保持一致的亮度倍增器！
            m_Data->DeferredLightingShader->SetFloat("u_Intensity", 30000.0f); 
        }

        if (m_Data->PrefilterMap) {
            m_Data->PrefilterMap->Bind(5); 
            m_Data->DeferredLightingShader->SetInt("u_PrefilteredMap", 5);
        }
        if (m_Data->BRDFLUT) {
            m_Data->BRDFLUT->Bind(6); 
            m_Data->DeferredLightingShader->SetInt("u_BRDFLUT", 6);
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
        // AYAYA_CORE_ERROR("showSkybox: {0}", showSkybox);
        
        if (showSkybox) {
            glDepthFunc(GL_LEQUAL);  
            m_Data->SkyboxShader->Bind();
            // ==========================================
            // 【新增】：向天空盒注入物理能量倍增器
            // ==========================================
            // 因为当前相机的 EV100 高达 14.5，我们需要极高的亮度才能被看见。
            // 这里暂定 30000.0f，后续可移入 Scene 的 Environment 属性中由用户调节。
            m_Data->SkyboxShader->SetFloat("u_Intensity", 30000.0f);

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

        if (showGrid) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthFunc(GL_LESS);  
            glDepthMask(GL_FALSE); 
            glDisable(GL_CULL_FACE);
            
            m_Data->GridShader->Bind();
            glm::mat4 gridTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1000.0f, 1.0f, 1000.0f));
            Renderer::Submit(m_Data->GridShader, m_Data->GridMesh->GetVertexArray(), gridTransform);
            
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE); 
            glDisable(GL_BLEND); 
        }

        m_Data->SelectionFBO->Bind();
        // 清空为纯透明黑色
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        RenderCommand::Clear();

        if (hoveredEntity && hoveredEntity.HasComponent<MeshRendererComponent>() && hoveredEntity.IsActiveInHierarchy()) {
            
            // 关闭深度测试，实现穿透墙壁的 X-Ray 选择效果！
            glDisable(GL_DEPTH_TEST); 
            m_Data->OutlineShader->Bind();
            
            // 直接输出纯白色剪影，不再需要算什么物理曝光！
            m_Data->OutlineShader->SetFloat3("u_Color", glm::vec3(1.0f, 1.0f, 1.0f)); 

            glm::mat4 transform = hoveredEntity.GetWorldTransform();
            auto& meshComp = hoveredEntity.GetComponent<MeshRendererComponent>();
            if (meshComp.ModelAsset) {
                for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                    Renderer::Submit(m_Data->OutlineShader, mesh->GetVertexArray(), transform);
                }
            }
        }
        glEnable(GL_DEPTH_TEST);
        m_Data->SelectionFBO->Unbind();

        m_Data->LightingFBO->Unbind();

        // ==========================================
        // Pass 6: Post-Processing Pass
        // ==========================================
        m_Data->PostProcessFBO->Bind();
        // PostProcessFBO 只需要清空颜色即可，不需要清空深度
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        RenderCommand::Clear();

        float currentEV100 = 14.5f;
        auto cameraView = scene->Reg().view<CameraComponent>();
        for (auto entityID : cameraView) {
            auto& cc = cameraView.get<CameraComponent>(entityID);
            if (cc.Primary) { currentEV100 = cc.EV100; break; }
        }

        glDisable(GL_DEPTH_TEST); 
        m_Data->PostProcessShader->Bind();
        m_Data->PostProcessShader->SetInt("u_ScreenTexture", 0);
        float physicalExposure = 1.0f / (1.2f * std::exp2(currentEV100));
        m_Data->PostProcessShader->SetFloat("u_Exposure", physicalExposure);
        m_Data->PostProcessShader->SetInt("u_SelectionTexture", 1);

        glm::vec2 texelSize = {
            1.0f / (float)m_Data->PostProcessFBO->GetSpecification().Width,
            1.0f / (float)m_Data->PostProcessFBO->GetSpecification().Height
        };
        // AYAYA_CORE_ERROR("Width: {0}, Height: {1}", m_Data->PostProcessFBO->GetSpecification().Width, m_Data->PostProcessFBO->GetSpecification().Height);
        m_Data->PostProcessShader->SetFloat2("u_TexelSize", texelSize);

        // 绑定 0 号槽位：Lighting FBO 的 HDR 输出
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_Data->LightingFBO->GetColorAttachmentRendererID(0));

        // 【新增】绑定 1 号槽位：刚刚画好的剪影图
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_Data->SelectionFBO->GetColorAttachmentRendererID(0));

        glBindVertexArray(m_Data->EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        
        m_Data->PostProcessFBO->Unbind();
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