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

    // 【极其重要的 C++ 内存对齐】：
    // OpenGL 的 std140 布局要求极其严格。vec3 在显存中会被按 16 字节(vec4)对齐！
    // 所以必须手动加上 float padding，否则数据会完全错位！
    struct struct_CameraData {
        glm::mat4 ViewProjection; // 64 bytes
        glm::vec3 CameraPosition; // 12 bytes
        float _padding;           // 4 bytes (凑齐 16 字节)
    };

    // ==========================================
    // 新增：平行光 UBO 结构体 (严格遵循 std140)
    // ==========================================
    struct struct_DirectionalLightData {
        glm::vec3 LightDir;       // 12 bytes
        float _padding1;          // 4 bytes (凑齐 16 字节)
        glm::vec3 LightColor;     // 12 bytes
        float _padding2;          // 4 bytes (凑齐 16 字节)
    };

    // ==========================================
    // 新增：渲染指令包 (用于收集和排序)
    // ==========================================
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
        std::shared_ptr<UniformBuffer> DirectionalLightUniformBuffer;
        struct_DirectionalLightData DirectionalLightData;

        // ==========================================
        // 新增：全局渲染队列
        // ==========================================
        std::vector<RenderCommandData> OpaqueDrawList;
    };

    static SceneRendererData s_Data;

    void SceneRenderer::Init() {
        // 1. 初始化纯白贴图
        s_Data.WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff; 
        s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        // 2. 初始化 3D 网格地基
        s_Data.GridMesh = Mesh::CreateCube(1.0f);

        // 3. 加载管线必需的 Shader
        // 注意：这里需要确保你的 Shader 类有 Create 方法，或者你继续使用 ShaderLibrary。
        // 为了简便，我们直接使用 Shader::Create
        s_Data.DefaultShader = Shader::Create("assets/Editor/shaders/PBR/pbr.vert", "assets/Editor/shaders/PBR/pbr.frag");
        s_Data.OutlineShader = Shader::Create("assets/Editor/shaders/UI/outline.vert", "assets/Editor/shaders/UI/outline.frag");
        s_Data.GridShader    = Shader::Create("assets/Editor/shaders/UI/grid.vert", "assets/Editor/shaders/UI/grid.frag");

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
        s_Data.DefaultShader->BindUniformBlock("Camera", 0);
        s_Data.OutlineShader->BindUniformBlock("Camera", 0);
        s_Data.GridShader->BindUniformBlock("Camera", 0);
        s_Data.FallbackShader->BindUniformBlock("Camera", 0);
        s_Data.SkyboxShader->BindUniformBlock("Camera", 0);

        // 新增：初始化 LightData UBO，绑定到 1 号槽位
        s_Data.DirectionalLightUniformBuffer = UniformBuffer::Create(sizeof(struct_DirectionalLightData), 1);
        s_Data.DefaultShader->BindUniformBlock("DirectionalLight", 1);
    }

   void SceneRenderer::BeginScene(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPosition) {
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

    void SceneRenderer::RenderScene(const std::shared_ptr<Scene>& scene, Entity hoveredEntity, bool showGrid) {

        // ==========================================
        // Pass 1: Lighting Setup Pass (收集场景灯光)
        // ==========================================
        glm::vec3 lightDir = { -0.2f, -1.0f, -0.3f }; 
        glm::vec3 lightColor = { 1.0f, 1.0f, 1.0f };
        float lightIntensity = 3.0f; // PBR 中光照强度需要大于 1 才能看出明亮的高光！

        auto lightGroup = scene->Reg().view<TransformComponent, DirectionalLightComponent>();
        for (auto entityID : lightGroup) {
            Entity lightEntity{ entityID, scene.get() };
            auto& transform = lightEntity.GetComponent<TransformComponent>();
            auto& dlc = lightEntity.GetComponent<DirectionalLightComponent>();

            glm::quat orientation = glm::quat(transform.Rotation);
            lightDir = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
            lightColor = dlc.Color;
            break; 
        }

        // ==========================================
        // 核心优化：直接将整理好的灯光数据打包推入 UBO！
        // ==========================================
        s_Data.DirectionalLightData.LightDir = lightDir;
        s_Data.DirectionalLightData.LightColor = lightColor * lightIntensity;
        s_Data.DirectionalLightUniformBuffer->SetData(&s_Data.DirectionalLightData, sizeof(struct_DirectionalLightData));
        // 注：这里彻底删掉了 s_Data.DefaultShader->Bind() 和 SetFloat3
        // 因为 UBO 的更新完全不需要先绑定 Shader！它存在于独立显存中。

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
        glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);

        // ==========================================
        // Pass 2: Geometry Pass (基于渲染队列的现代管线)
        // ==========================================
        // 每次渲染前清空上一帧的队列
        s_Data.OpaqueDrawList.clear();

        // 根据当前相机的 ViewProjection 矩阵生成视锥体 (你上一步实现的)
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

            // 如果没有模型资产，直接跳过
            if (!meshComp.ModelAsset) continue;

            // 视锥体剔除：只要有一个 SubMesh 可见，我们就把它加入队列
            bool isVisible = false;
            if (meshComp.ModelAsset) {
                for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                    totalMeshes++;
                    if (cameraFrustum.IsBoxVisible(mesh->GetAABB(), transform)) {
                        isVisible = true; // 只要模型里有一个网格可见，我们就渲染它
                        drawnMeshes++;
                        break;
                    }
                }
            }
            if (!isVisible) continue;

            // 判断使用的 Shader 和 Material
            bool isFallback = (!meshComp.MaterialAsset || meshComp.MaterialAsset->Properties.empty());
            std::shared_ptr<Shader> targetShader = isFallback ? s_Data.FallbackShader : s_Data.DefaultShader;
            std::shared_ptr<Material> targetMaterial = isFallback ? nullptr : meshComp.MaterialAsset;

            // 将模型中包含的所有网格拆解为独立的渲染指令，压入队列
            for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                RenderCommandData cmd;
                cmd.Transform = transform;
                cmd.MeshAsset = mesh;
                cmd.MaterialAsset = targetMaterial;
                cmd.ShaderAsset = targetShader;
                cmd.TargetEntity = entity;

                s_Data.OpaqueDrawList.push_back(cmd);
            }
        }

        // ------------------------------------------
        // 阶段 2.2：状态排序 (State Sorting)
        // ------------------------------------------
        // 排序优先级：Shader -> Material -> Mesh
        // 我们直接比较智能指针的底层内存地址 (.get())，这是极其高效的 O(1) 比较！
        std::sort(s_Data.OpaqueDrawList.begin(), s_Data.OpaqueDrawList.end(), [](const RenderCommandData& a, const RenderCommandData& b) {
            if (a.ShaderAsset.get() != b.ShaderAsset.get())
                return a.ShaderAsset.get() < b.ShaderAsset.get();
            
            if (a.MaterialAsset.get() != b.MaterialAsset.get())
                return a.MaterialAsset.get() < b.MaterialAsset.get();
            
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
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glStencilMask(0xFF); 
            } else {
                glStencilFunc(GL_ALWAYS, 0, 0xFF);
                glStencilMask(0x00); 
            }

            // 2. 只有当 Shader 发生变化时，才调用昂贵的 Bind()
            if (currentShader != cmd.ShaderAsset) {
                currentShader = cmd.ShaderAsset;
                currentShader->Bind();
            }

            // 3. 只有当 Material 发生变化时，才重新上传 Uniform 和绑定贴图
            if (currentMaterial != cmd.MaterialAsset) {
                currentMaterial = cmd.MaterialAsset;
                
                if (currentMaterial) { // 如果不是 Fallback 空材质
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
                            {
                                currentShader->SetInt(prop.UniformName, textureSlot);
                                if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                    auto tex = AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                                    tex->Bind(textureSlot);
                                } else {
                                    s_Data.WhiteTexture->Bind(textureSlot); // 或者使用你定义的 DefaultNormalTexture
                                }
                                textureSlot++;
                                break;
                            }
                            default: break;
                        }
                    }
                }
            }

            // 4. 提交绘制！(Renderer 会负责绑定 VAO 和上传 Transform 矩阵)
            Renderer::Submit(currentShader, cmd.MeshAsset->GetVertexArray(), cmd.Transform);
        }

        // 剔除日志
        // AYAYA_CORE_TRACE("Culling: {0} / {1} meshes rendered", drawnMeshes, totalMeshes);
        // 恢复模板测试状态
        glStencilMask(0x00);
        glDisable(GL_STENCIL_TEST);

        // ==========================================
        // Pass 3: Skybox Pass (极其优雅的最后渲染)
        // ==========================================
        glDepthFunc(GL_LEQUAL);  
        
        s_Data.SkyboxShader->Bind();
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(s_Data.ViewMatrix));
        s_Data.SkyboxShader->SetMat4("u_View", viewNoTranslation);
        s_Data.SkyboxShader->SetMat4("u_Projection", s_Data.ProjectionMatrix);
        
        // --- 修复：解开之前被注释掉的环境贴图绑定！ ---
        if (s_Data.EnvironmentCubemap) {
            s_Data.EnvironmentCubemap->Bind(0); 
            s_Data.SkyboxShader->SetInt("u_Skybox", 0);
        }

        glDisable(GL_CULL_FACE); 
        Renderer::Submit(s_Data.SkyboxShader, s_Data.SkyboxMesh->GetVertexArray(), glm::mat4(1.0f));
        glEnable(GL_CULL_FACE);

        glDepthFunc(GL_LESS);

        // ==========================================
        // Pass 4: Background Grid Pass (渲染无限网格)
        // ==========================================
        if (showGrid) {
            // 1. 开启混合
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            
            // 2. 核心防御：严格规范深度测试状态！
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);  // 确保恢复到正常的"近挡远"逻辑（防止被天空盒的 GL_LEQUAL 污染）
            glDepthMask(GL_FALSE); // 网格自身不写入深度，但严格接受模型的深度遮挡！

            // 3. 核心防御：关闭模板测试（防止被上一层 Hover 描边的状态影响）
            glDisable(GL_STENCIL_TEST); 

            // 4. 渲染网格
            s_Data.GridShader->Bind();
            // 注意这里 Y 轴缩放为 0，所以网格严格位于 Y=0 的地平面
            glm::mat4 gridTransform = glm::scale(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 100.0f));
            Renderer::Submit(s_Data.GridShader, s_Data.GridMesh->GetVertexArray(), gridTransform);
            
            // 5. 乖乖还原状态，保持环境整洁
            glDepthMask(GL_TRUE); 
            glDisable(GL_BLEND); 
            glEnable(GL_STENCIL_TEST); 
        }

        // ==========================================
        // Pass 5: Outline Post-Pass (渲染描边高亮)
        // ==========================================
        if (hoveredEntity && hoveredEntity.HasComponent<MeshRendererComponent>()) {
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
            glStencilMask(0x00);      
            glDisable(GL_DEPTH_TEST); 

            s_Data.OutlineShader->Bind();
            s_Data.OutlineShader->SetFloat3("u_Color", glm::vec3(1.0f, 0.65f, 0.0f)); 
            
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
    }
}