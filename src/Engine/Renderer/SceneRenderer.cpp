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
        std::shared_ptr<Mesh> SkyboxCubeMesh;

        // 新增 UBO 相关的成员
        std::shared_ptr<UniformBuffer> CameraUniformBuffer;
        struct_CameraData CameraData;
        std::shared_ptr<UniformBuffer> LightUniformBuffer;
        struct_LightData LightData;

        // ==========================================
        // 核心升级 2：三套 FBO 形成流水线！
        // ==========================================
        std::shared_ptr<Framebuffer> GeometryFBO;    // 装载 4 张 G-Buffer 数据图
        std::shared_ptr<Framebuffer> LightingFBO;    // 装载光照合成结果 (HDR)
        std::shared_ptr<Framebuffer> PostProcessFBO; // 装载最终 LDR 画布
        
        std::shared_ptr<Shader> PostProcessShader;   // 后期 Shader
        uint32_t EmptyVAO;                           // 用于全屏绘制的空 VAO

        // ==========================================
        // 新增：全局渲染队列
        // ==========================================
        std::vector<RenderCommandData> OpaqueDrawList;
        // 新增：静态统计数据
        SceneRenderer::Statistics Stats;
    };

    static SceneRendererData s_Data;

    void SceneRenderer::Init() {
        // 1. 初始化纯白贴图
        s_Data.WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff; 
        s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        // 2. 初始化 3D 网格地基
        s_Data.GridMesh = Mesh::CreatePlane(1.0f, 1.0f);

        // 3. 加载管线必需的 Shader
        s_Data.DefaultShader = Shader::Create("assets/Editor/shaders/PBR/pbr.vert", "assets/Editor/shaders/PBR/pbr.frag");
        s_Data.OutlineShader = Shader::Create("assets/Editor/shaders/UI/outline.vert", "assets/Editor/shaders/UI/outline.frag");
        s_Data.GridShader    = Shader::Create("assets/Editor/shaders/UI/grid.vert", "assets/Editor/shaders/UI/grid.frag");
        // ==========================================
        // 加载全新的延迟渲染 Shader
        // ==========================================
        s_Data.GBufferShader = Shader::Create("assets/Editor/shaders/Deferred/gbuffer.vert", "assets/Editor/shaders/Deferred/gbuffer.frag");
        s_Data.DeferredLightingShader = Shader::Create("assets/Editor/shaders/Deferred/deferred_lighting.vert", "assets/Editor/shaders/Deferred/deferred_lighting.frag");

        // 4. 加载Fallback材质
        s_Data.FallbackShader = Shader::Create("assets/Editor/shaders/Fallback/fallback.vert", "assets/Editor/shaders/Fallback/fallback.frag");

        // 创建空材质并绑定 Fallback 标识
        s_Data.FallbackMaterial = std::make_shared<Material>();
        s_Data.FallbackMaterial->Name = "Error Fallback";
        s_Data.FallbackMaterial->ShaderName = "Fallback";

        // 5. 加载天空盒
        s_Data.SkyboxShader = Shader::Create("assets/Editor/shaders/Skybox/skybox.vert", "assets/Editor/shaders/Skybox/skybox.frag");
        // 加载天空盒纹理 (按 Right, Left, Top, Bottom, Front, Back 顺序)
        std::vector<std::string> faces = {
            "assets/textures/skybox/right.jpg",
            "assets/textures/skybox/left.jpg",
            "assets/textures/skybox/top.jpg",
            "assets/textures/skybox/bottom.jpg",
            "assets/textures/skybox/front.jpg",
            "assets/textures/skybox/back.jpg"
        };
        s_Data.EnvironmentCubemap = std::make_shared<TextureCube>(faces);
        s_Data.SkyboxMesh = Mesh::CreateCube(1.0f);

        // ==========================================
        // 6. 初始化 UBO
        // ==========================================
        s_Data.CameraUniformBuffer = UniformBuffer::Create(sizeof(struct_CameraData), 0);
        s_Data.GBufferShader->BindUniformBlock("Camera", 0);
        s_Data.DeferredLightingShader->BindUniformBlock("Camera", 0);
        s_Data.DefaultShader->BindUniformBlock("Camera", 0);
        s_Data.OutlineShader->BindUniformBlock("Camera", 0);
        s_Data.GridShader->BindUniformBlock("Camera", 0);
        s_Data.FallbackShader->BindUniformBlock("Camera", 0);
        s_Data.SkyboxShader->BindUniformBlock("Camera", 0);

        // // 新增：初始化 LightData UBO，绑定到 1 号槽位
        // s_Data.LightUniformBuffer = UniformBuffer::Create(sizeof(struct_LightData), 1);
        // s_Data.DefaultShader->BindUniformBlock("LightData", 1);

        s_Data.LightUniformBuffer = UniformBuffer::Create(sizeof(struct_LightData), 1);
        s_Data.DeferredLightingShader->BindUniformBlock("LightData", 1); // 光照数据只交给 LightingShader

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
        s_Data.GeometryFBO = Framebuffer::Create(geoSpec);

        // 7.2 Lighting FBO (合成光照，HDR 精度)
        FramebufferSpecification lightSpec;
        lightSpec.Samples = 1;
        lightSpec.Width = 1280; lightSpec.Height = 720;
        lightSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        s_Data.LightingFBO = Framebuffer::Create(lightSpec);

        // 3. PostProcess FBO (屏幕输出，LDR)
        FramebufferSpecification postSpec;
        postSpec.Samples = 1; 
        postSpec.Width = 1280; postSpec.Height = 720;
        postSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        s_Data.PostProcessFBO = Framebuffer::Create(postSpec);

        s_Data.PostProcessShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/postprocess.frag");

        // 创建一个空 VAO，供 gl_VertexID 魔法使用
        glGenVertexArrays(1, &s_Data.EmptyVAO);
    }

    void SceneRenderer::OnWindowResize(uint32_t width, uint32_t height) {
        s_Data.GeometryFBO->Resize(width, height);
        s_Data.LightingFBO->Resize(width, height);
        s_Data.PostProcessFBO->Resize(width, height);
    }

    void SceneRenderer::SetMSAASamples(uint32_t samples) {
        // 延迟管线暂不开启硬件 MSAA，我们强制锁死为 1，后期用 FXAA 替代
        auto spec = s_Data.GeometryFBO->GetSpecification();
        // spec.Samples = samples;
        spec.Samples = 1;
        s_Data.GeometryFBO = Framebuffer::Create(spec); // 直接重建
    }

    uint32_t SceneRenderer::GetFinalColorAttachmentRendererID() {
        return s_Data.PostProcessFBO->GetColorAttachmentRendererID();
    }

   void SceneRenderer::BeginScene(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPosition) {
        // 在每帧渲染开始时，清零统计计数器
        ResetStats();

        // 保存分开的矩阵
        s_Data.ViewMatrix = viewMatrix;
        s_Data.ProjectionMatrix = projectionMatrix;
        s_Data.ViewProjectionMatrix = projectionMatrix * viewMatrix; 
        s_Data.CameraPosition = cameraPosition;
        
        // ==========================================
        // 核心：ubo 每帧只在这里把相机数据传给显卡一次！
        // ==========================================
        s_Data.CameraData.ViewProjection = s_Data.ViewProjectionMatrix;
        s_Data.CameraData.CameraPosition = s_Data.CameraPosition;
        s_Data.CameraUniformBuffer->SetData(&s_Data.CameraData, sizeof(struct_CameraData));

        Renderer::BeginScene(s_Data.ViewProjectionMatrix);
    }

    void SceneRenderer::EndScene() {
        Renderer::EndScene();
    }

    void SceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene, Entity hoveredEntity, bool showGrid, bool showSkybox, const glm::vec4& clearColor) {

        // ==========================================
        // Pass 1: Lighting Setup Pass (收集场景灯光)
        // ==========================================
        // 清理旧数据
        memset(&s_Data.LightData, 0, sizeof(struct_LightData));
        
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
            s_Data.LightData.DirLightDir = glm::vec4(dir, dlc.AmbientStrength);
            s_Data.LightData.DirLightColor = glm::vec4(dlc.Color * dlc.Illuminance, 0.0f);
            hasDirLight = true;
            break; 
        }

        // 如果场景里没有平行光（比如室内棚拍），给一个微弱的默认环境光
        if (!hasDirLight) {
            s_Data.LightData.DirLightDir = glm::vec4(0.0f, -1.0f, 0.0f, 0.03f);
        }

        // 2. 收集点光源
        int pointLightIndex = 0;
        auto pointLightGroup = scene->Reg().view<TransformComponent, PointLightComponent>();
        for (auto entityID : pointLightGroup) {
            if (pointLightIndex >= 4) break; 
            auto [transform, plc] = pointLightGroup.get<TransformComponent, PointLightComponent>(entityID);
            s_Data.LightData.PointLights[pointLightIndex].Position = glm::vec4(transform.Translation, 1.0f);
            
            // 物理换算：流明 (Lumens) 转换为 坎德拉 (Candelas)，供 shader 做平方反比衰减
            float candelas = plc.LuminousPower / (4.0f * glm::pi<float>());
            s_Data.LightData.PointLights[pointLightIndex].Color = glm::vec4(plc.Color * candelas, 1.0f);
            pointLightIndex++;
        }
        s_Data.LightData.PointLightCount = pointLightIndex;
        s_Data.LightUniformBuffer->SetData(&s_Data.LightData, sizeof(struct_LightData));


        // ==========================================
        // Pass 2: Geometry Pass (G-Buffer)
        // ==========================================
        s_Data.GeometryFBO->Bind();
        // 极度关键：坐标贴图的 Alpha 清理为 0，代表天空！
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        RenderCommand::Clear();
        glClear(GL_STENCIL_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
        glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);

        s_Data.OpaqueDrawList.clear();
        // 根据当前相机的 ViewProjection 矩阵生成视锥体
        Frustum cameraFrustum(s_Data.ViewProjectionMatrix);

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
            std::shared_ptr<Shader> targetShader = isFallback ? s_Data.FallbackShader : s_Data.GBufferShader;
            std::shared_ptr<Material> targetMaterial = isFallback ? nullptr : meshComp.MaterialAsset;

           for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                RenderCommandData cmd = { transform, mesh, targetMaterial, targetShader, entity };
                s_Data.OpaqueDrawList.push_back(cmd);
            }
        }

        // ------------------------------------------
        // 阶段 2.2：状态排序 (State Sorting)
        // ------------------------------------------
        // 排序优先级：Shader -> Material -> Mesh
        // 我们直接比较智能指针的底层内存地址 (.get())，这是极其高效的 O(1) 比较！
       std::sort(s_Data.OpaqueDrawList.begin(), s_Data.OpaqueDrawList.end(), [](const auto& a, const auto& b) {
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

        for (const auto& cmd : s_Data.OpaqueDrawList) {
            
            // 1. 处理悬停描边遮罩 (每个物体的 EntityID 可能不同，必须每帧判断)
            if (hoveredEntity && hoveredEntity == cmd.TargetEntity) {
                glStencilFunc(GL_ALWAYS, 1, 0xFF); glStencilMask(0xFF); 
            } else {
                glStencilFunc(GL_ALWAYS, 0, 0xFF); glStencilMask(0x00); 
            }

            // 2. 只有当 Shader 发生变化时，才调用昂贵的 Bind()
            if (currentShader != cmd.ShaderAsset) {
                currentShader = cmd.ShaderAsset;
                currentShader->Bind();
                s_Data.Stats.ShaderBinds++;
            }

            // 3. 只有当 Material 发生变化时，才重新上传 Uniform 和绑定贴图
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
                                    s_Data.WhiteTexture->Bind(textureSlot); 
                                }
                                textureSlot++;
                                break;
                            default: break;
                        }
                    }
                }
            }

            // 4. 提交绘制！(Renderer 会负责绑定 VAO 和上传 Transform 矩阵)
            Renderer::Submit(currentShader, cmd.MeshAsset->GetVertexArray(), cmd.Transform);

            // 记录实体模型的绘制
            s_Data.Stats.DrawCalls++;
            s_Data.Stats.VertexCount += cmd.MeshAsset->GetVertexCount();
            s_Data.Stats.TriangleCount += cmd.MeshAsset->GetIndexCount() / 3;
        }

        // 剔除日志
        // AYAYA_CORE_TRACE("Culling: {0} / {1} meshes rendered", drawnMeshes, totalMeshes);
        // 恢复模板测试状态
        glStencilMask(0x00);
        glDisable(GL_STENCIL_TEST);
        s_Data.GeometryFBO->Unbind();

        // ==========================================
        // Pass 3: Deferred Lighting Pass (光照核爆合成)
        // ==========================================
        s_Data.LightingFBO->Bind();
        // 用相机的实际背景色清屏！
        RenderCommand::SetClearColor(clearColor);
        RenderCommand::Clear();

        // 画全屏四边形，绝对不能开启深度测试！
        glDisable(GL_DEPTH_TEST);
        s_Data.DeferredLightingShader->Bind();

        // 将 G-Buffer 塞进插槽
        s_Data.DeferredLightingShader->SetInt("g_Position", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_Data.GeometryFBO->GetColorAttachmentRendererID(0));

        s_Data.DeferredLightingShader->SetInt("g_Normal", 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, s_Data.GeometryFBO->GetColorAttachmentRendererID(1));

        s_Data.DeferredLightingShader->SetInt("g_Albedo", 2);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, s_Data.GeometryFBO->GetColorAttachmentRendererID(2));

        s_Data.DeferredLightingShader->SetInt("g_PBR", 3);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, s_Data.GeometryFBO->GetColorAttachmentRendererID(3));

        glBindVertexArray(s_Data.EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        
        s_Data.Stats.DrawCalls++;
        s_Data.Stats.VertexCount += 3;
        
        glEnable(GL_DEPTH_TEST); // 画完恢复深度测试
        // 注意：不解绑，我们要继续在这个 FBO 上画前方物理世界的其余东西！

        // ==========================================
        // Pass 4: Depth & Stencil Blit (极其黑科技)
        // ==========================================
        // 我们把 G-Buffer 里的深度图复制过来，这样等会画网格和天空盒时才知道怎么遮挡！
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_Data.GeometryFBO->GetRendererID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_Data.LightingFBO->GetRendererID());

        uint32_t width = s_Data.LightingFBO->GetSpecification().Width;
        uint32_t height = s_Data.LightingFBO->GetSpecification().Height;

        glBlitFramebuffer(0, 0, width, height,
                          0, 0, width, height,
                          GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);

        // 重新确立 Lighting FBO 的绘制焦点
        glBindFramebuffer(GL_FRAMEBUFFER, s_Data.LightingFBO->GetRendererID());

        // ==========================================
        // Pass 5: Forward Pass (天空盒、网格、描边)
        // ==========================================
        // AYAYA_CORE_ERROR("showSkybox: {0}", showSkybox);
        
        if (showSkybox) {
            glDepthFunc(GL_LEQUAL);  
            s_Data.SkyboxShader->Bind();
            glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(s_Data.ViewMatrix));
            s_Data.SkyboxShader->SetMat4("u_View", viewNoTranslation);
            
            glm::mat4 skyboxProjection = s_Data.ProjectionMatrix;
            if (s_Data.ProjectionMatrix[3][3] == 1.0f) {
                float aspect = (float)s_Data.GeometryFBO->GetSpecification().Width / (float)s_Data.GeometryFBO->GetSpecification().Height;
                skyboxProjection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
            }
            s_Data.SkyboxShader->SetMat4("u_Projection", skyboxProjection);
            
            if (s_Data.EnvironmentCubemap) {
                s_Data.EnvironmentCubemap->Bind(0); 
                s_Data.SkyboxShader->SetInt("u_Skybox", 0);
            }

            glDisable(GL_CULL_FACE); 
            Renderer::Submit(s_Data.SkyboxShader, s_Data.SkyboxMesh->GetVertexArray(), glm::mat4(1.0f));
            glEnable(GL_CULL_FACE);
            glDepthFunc(GL_LESS);
            
            s_Data.Stats.DrawCalls++;
            s_Data.Stats.VertexCount += 36; 
        }

        if (showGrid) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthFunc(GL_LESS);  
            glDepthMask(GL_FALSE); 
            glDisable(GL_STENCIL_TEST); 
            glDisable(GL_CULL_FACE);
            
            s_Data.GridShader->Bind();
            glm::mat4 gridTransform = glm::scale(glm::mat4(1.0f), glm::vec3(1000.0f, 1.0f, 1000.0f));
            Renderer::Submit(s_Data.GridShader, s_Data.GridMesh->GetVertexArray(), gridTransform);
            
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE); 
            glDisable(GL_BLEND); 
            glEnable(GL_STENCIL_TEST); 
        }

        if (hoveredEntity && hoveredEntity.HasComponent<MeshRendererComponent>() && hoveredEntity.IsActiveInHierarchy()) {
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
            glStencilMask(0x00);      
            glDisable(GL_DEPTH_TEST); 

            s_Data.OutlineShader->Bind();
            // ==========================================
            // 修复：提取当前相机的曝光值，计算逆曝光倍数
            // ==========================================
            float currentEV100 = 14.5f;
            auto cameraView = scene->Reg().view<CameraComponent>();
            for (auto entityID : cameraView) {
                auto& cc = cameraView.get<CameraComponent>(entityID);
                if (cc.Primary) { currentEV100 = cc.EV100; break; }
            }
            
            // 计算曝光的逆反值 (与 Pass 6 的公式倒过来)
            float inverseExposure = 1.2f * std::exp2(currentEV100);
            s_Data.OutlineShader->SetFloat3("u_Color", glm::vec3(1.0f, 0.65f, 0.0f) * inverseExposure); 
            glm::mat4 baseTransform = hoveredEntity.GetWorldTransform();
            glm::mat4 transform = baseTransform * glm::scale(glm::mat4(1.0f), glm::vec3(1.05f)); 
            
            auto& meshComp = hoveredEntity.GetComponent<MeshRendererComponent>();
            if (meshComp.ModelAsset) {
                for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                    Renderer::Submit(s_Data.OutlineShader, mesh->GetVertexArray(), transform);
                }
            }

            glStencilMask(0xFF);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
        }

        s_Data.LightingFBO->Unbind();

        // ==========================================
        // Pass 6: Post-Processing Pass
        // ==========================================
        s_Data.PostProcessFBO->Bind();
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
        s_Data.PostProcessShader->Bind();
        s_Data.PostProcessShader->SetInt("u_ScreenTexture", 0);
        float physicalExposure = 1.0f / (1.2f * std::exp2(currentEV100));
        s_Data.PostProcessShader->SetFloat("u_Exposure", physicalExposure);

        // 读取 Lighting FBO 的 HDR 输出
        uint32_t hdrTexture = s_Data.LightingFBO->GetColorAttachmentRendererID(0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrTexture);

        glBindVertexArray(s_Data.EmptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        
        s_Data.PostProcessFBO->Unbind();
    }

    uint32_t SceneRenderer::GetPostProcessFBORendererID() {
        return s_Data.PostProcessFBO->GetRendererID();
    }

    void SceneRenderer::ResetStats() {
        memset(&s_Data.Stats, 0, sizeof(Statistics));
    }

    SceneRenderer::Statistics SceneRenderer::GetStats() {
        return s_Data.Stats;
    }
}