#include "EditorLayer.hpp"
#include "Renderer/Mesh.hpp"

#include <glad/glad.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Ayaya {

    EditorLayer::EditorLayer() : Layer("EditorLayer") {}

    void EditorLayer::OnAttach() {
        // ==========================================
        // 启动时读取资产注册表
        // ==========================================
        AssetManager::DeserializeRegistry("assets/AssetRegistry.yaml");

        FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        // ==========================================
        // 初始化纯白贴图 (1x1 像素)
        // ==========================================
        m_WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff; // 16进制表示：RGBA全部拉满 (255, 255, 255, 255)
        m_WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        m_ShaderLibrary.Load("assets/shaders/default.vert", "assets/shaders/default.frag");
        m_ShaderLibrary.Load("assets/shaders/outline.vert", "assets/shaders/outline.frag");
        m_ShaderLibrary.Load("assets/shaders/grid.vert", "assets/shaders/grid.frag");
        m_ShaderLibrary.Load("assets/shaders/lighting.vert", "assets/shaders/lighting.frag");

        SetupGeometry();
        SetupScene();
    }

    void EditorLayer::OnUpdate(Timestep ts) {
        HandleShortcuts();

        // ==========================================
        // 核心修复 2：在一切渲染开始前，检查并处理 Resize
        // 这样新建的 Framebuffer 马上就会在下面的代码中被渲染塞满，绝对不会黑屏！
        // ==========================================
        static glm::vec2 s_LastViewportSize = { 0.0f, 0.0f };
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f && 
           (s_LastViewportSize.x != m_ViewportSize.x || s_LastViewportSize.y != m_ViewportSize.y)) {
            
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_EditorCamera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
            
            auto view = m_ActiveScene->Reg().view<CameraComponent>();
            for (auto entityID : view) {
                auto& cameraComp = view.get<CameraComponent>(entityID);
                if (!cameraComp.FixedAspectRatio) {
                    cameraComp.Camera.SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                }
            }
            s_LastViewportSize = m_ViewportSize;
        }

        m_EditorCamera.OnUpdate(ts, m_ViewportFocused);

        m_Framebuffer->Bind();
        RenderCommand::SetClearColor({ 0.12f, 0.12f, 0.14f, 1.0f });
        RenderCommand::Clear();
        glClear(GL_STENCIL_BUFFER_BIT); 

        // 2. 直接拿它的矩阵去渲染世界！
        RenderScene(m_EditorCamera.GetViewProjection());

        m_Framebuffer->Unbind();

        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        RenderCommand::Clear();
    }

    void EditorLayer::OnImGuiRender() {
        UIRenderDockspace();
        UIRenderMenuBar();

        m_SceneHierarchyPanel.OnImGuiRender();
        m_ContentBrowserPanel.OnImGuiRender();

        UIRenderViewport();

        ImGui::End(); // End DockSpace
    }

    void EditorLayer::OnEvent(Event& event) {}

    void EditorLayer::SetupGeometry() {
        // ==========================================
        // 清爽！直接向 Mesh 类要一个正方体的顶点数组
        // ==========================================
        std::shared_ptr<Mesh> cubeMesh = Mesh::CreateCube(1.0f);
        m_VertexArray = cubeMesh->GetVertexArray();
    }

    void EditorLayer::SetupScene() {
        m_ActiveScene = std::make_shared<Scene>();

        // ==========================================
        // 核心修复：不要直接 Create 和 AddAsset，
        // 必须调用 ImportAsset 让它登记进硬盘账本！
        // ==========================================
        UUID bricksHandle = AssetManager::ImportAsset("assets/textures/bricks2.jpg");

        // 创造摄像机
        Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<CameraComponent>();
        cameraComp.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        cameraComp.Camera.SetViewportSize(1280, 720);
        auto& cameraTransform = cameraEntity.GetComponent<TransformComponent>();
        cameraTransform.Translation = { 0.0f, 0.0f, 5.0f };

        // 创造太阳光
        Entity dirLight = m_ActiveScene->CreateEntity("Directional Light");
        auto& lightTransform = dirLight.GetComponent<TransformComponent>();
        lightTransform.Rotation = glm::radians(glm::vec3(-45.0f, 45.0f, 0.0f));
        dirLight.AddComponent<DirectionalLightComponent>();

        // 创造场景物体
        Entity parentNode = m_ActiveScene->CreateEntity("Parent Empty Node");

        // --- 左边的方块 (带砖块贴图) ---
        Entity square1 = m_ActiveScene->CreateEntity("Left Square");
        square1.GetComponent<TransformComponent>().Translation = { -1.5f, 0.0f, 0.0f };
        
        auto& mrc1 = square1.AddComponent<MeshRendererComponent>(); // 添加 3D 网格组件
        mrc1.Color = glm::vec4{0.2f, 0.8f, 0.3f, 1.0f};             // 设置绿色混合
        mrc1.TextureHandle = bricksHandle;                          // 赋予砖块贴图
        
        square1.SetParent(parentNode); 

        // --- 右边的方块 (纯色) ---
        Entity square2 = m_ActiveScene->CreateEntity("Right Square");
        square2.GetComponent<TransformComponent>().Translation = { 1.5f, 0.0f, 0.0f };
        
        auto& mrc2 = square2.AddComponent<MeshRendererComponent>(); // 添加 3D 网格组件
        mrc2.Color = glm::vec4{0.8f, 0.2f, 0.3f, 1.0f};             // 设置红色

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    // =====================================================================
    // 智能保存逻辑
    // =====================================================================
    void EditorLayer::SaveScene() {
        // 如果当前路径不为空，直接静默保存覆写
        if (!m_CurrentScenePath.empty()) {
            SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(m_CurrentScenePath);
            AYAYA_CORE_INFO("Scene strictly saved to {0}", m_CurrentScenePath);
        } 
        // 否则（这是一个新建的未保存场景），转为“另存为”逻辑
        else {
            SaveSceneAs();
        }
    }

    void EditorLayer::SaveSceneAs() {
        std::string defaultName = "Untitled.ayaya";
        
        // 如果当前场景已经有路径，提取它的文件名作为默认名字
        if (!m_CurrentScenePath.empty()) {
            size_t pos = m_CurrentScenePath.find_last_of("/\\");
            defaultName = pos != std::string::npos ? m_CurrentScenePath.substr(pos + 1) : m_CurrentScenePath;
        }

        // 呼出带默认名字的原生保存弹窗
        std::string filepath = FileDialogs::SaveFile("ayaya", defaultName);
        
        if (!filepath.empty()) { 
            SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(filepath);
            m_CurrentScenePath = filepath; // 更新当前工作路径
            AYAYA_CORE_INFO("Scene saved as to {0}", filepath);
        }
    }

    void EditorLayer::NewScene() {
        m_ActiveScene = std::make_shared<Scene>();

        Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<CameraComponent>();
        cameraComp.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f) {
            cameraComp.Camera.SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        }
        
        auto& cameraTransform = cameraEntity.GetComponent<TransformComponent>();
        cameraTransform.Translation = { 0.0f, 0.0f, 5.0f };

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
        m_CurrentScenePath = std::string(); 
        m_HoveredEntity = {};
        m_SceneHierarchyPanel.SetSelectedEntity({});
        AYAYA_CORE_INFO("Created a new empty scene with default camera.");
    }

    void EditorLayer::OpenScene() {
        std::string filepath = FileDialogs::OpenFile("ayaya"); 
        if (!filepath.empty()) { 
            std::shared_ptr<Scene> newScene = std::make_shared<Scene>();
            SceneSerializer serializer(newScene);
            if (serializer.Deserialize(filepath)) {
                m_ActiveScene = newScene;
                
                auto view = m_ActiveScene->Reg().view<CameraComponent>();
                for (auto entityID : view) {
                    auto& cameraComp = view.get<CameraComponent>(entityID);
                    cameraComp.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
                    if (!cameraComp.FixedAspectRatio) {
                        cameraComp.Camera.SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                    }
                }

                m_SceneHierarchyPanel.SetContext(m_ActiveScene);
                m_CurrentScenePath = filepath;
                m_HoveredEntity = {};
                m_SceneHierarchyPanel.SetSelectedEntity({});
                AYAYA_CORE_INFO("Scene loaded successfully from {0}!", filepath);
            }
        }
    }

    void EditorLayer::HandleShortcuts() {
        // =====================================
        // 1. 视口焦点相关的快捷键 (Gizmo 等)
        // =====================================
        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selectedEntity && !Input::IsMouseButtonPressed(1)) {
            if (Input::IsKeyPressed(Key::Q)) m_GizmoType = -1;
            if (Input::IsKeyPressed(Key::W)) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            if (Input::IsKeyPressed(Key::E)) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            if (Input::IsKeyPressed(Key::R)) m_GizmoType = ImGuizmo::OPERATION::SCALE;
        }

        // =====================================
        // 2. 全局场景快捷键 (绕过 ImGui 事件拦截)
        // =====================================
        bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl) ||
                       Input::IsKeyPressed(Key::LeftSuper)   || Input::IsKeyPressed(Key::RightSuper);
        bool shift   = Input::IsKeyPressed(Key::LeftShift)   || Input::IsKeyPressed(Key::RightShift);

        // 使用静态变量记录上一帧的按键状态，实现“按下瞬间”单次触发 (Edge Detection)
        static bool s_N_Pressed = false;
        static bool s_O_Pressed = false;
        static bool s_S_Pressed = false;

        // --- New Scene (Cmd + N) ---
        if (Input::IsKeyPressed(Key::N)) {
            if (!s_N_Pressed && control) {
                AYAYA_CORE_INFO("👉 Shortcut Triggered: New Scene");
                NewScene();
            }
            s_N_Pressed = true; // 锁定，只要不松手就不会再次触发
        } else {
            s_N_Pressed = false; // 松手后解锁
        }

        // --- Open Scene (Cmd + O) ---
        if (Input::IsKeyPressed(Key::O)) {
            if (!s_O_Pressed && control) {
                AYAYA_CORE_INFO("👉 Shortcut Triggered: Open Scene");
                OpenScene();
            }
            s_O_Pressed = true;
        } else {
            s_O_Pressed = false;
        }

        // --- Save / Save As (Cmd + S / Cmd + Shift + S) ---
        if (Input::IsKeyPressed(Key::S)) {
            if (!s_S_Pressed && control) {
                if (shift) {
                    AYAYA_CORE_INFO("👉 Shortcut Triggered: Save Scene As...");
                    SaveSceneAs();
                } else {
                    AYAYA_CORE_INFO("👉 Shortcut Triggered: Save Scene");
                    SaveScene();
                }
            }
            s_S_Pressed = true;
        } else {
            s_S_Pressed = false;
        }
    }

    void EditorLayer::RenderScene(const glm::mat4& cameraViewProj) {
        Renderer::BeginScene(cameraViewProj);

        // ==========================================
        // 核心：在所有物体之前渲染背景网格
        // ==========================================
        if (m_ShowGrid) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST);
            
            // 关闭深度写入，防止半透明的网格遮挡后面的物体
            glDepthMask(GL_FALSE);

            auto gridShader = m_ShaderLibrary.Get("grid");
            gridShader->Bind();
            
            // 魔法：把我们之前的 Cube (正方体) 强行拍扁成 Y=0 的厚度！
            // 并在 X 和 Z 方向各放大 100 倍，铺成一张无限大平原
            glm::mat4 gridTransform = glm::scale(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 100.0f));
                                    
            Renderer::Submit(gridShader, m_VertexArray, gridTransform);
            
            // 恢复深度写入，准备渲染正常的游戏物体
            glDepthMask(GL_TRUE); 
        }

        // auto defaultShader = m_ShaderLibrary.Get("default");
        // defaultShader->Bind();
        // defaultShader->SetInt("u_Texture", 0);

        auto lightingShader = m_ShaderLibrary.Get("lighting");
        lightingShader->Bind();
        lightingShader->SetInt("u_Texture", 0);
        // ==========================================
        // 核心魔法：上帝说，要有光！
        // ==========================================
        // ==========================================
        // 核心魔法：从 ECS 场景中寻找平行光！
        // ==========================================
        // 先设置一个默认的后备光源（防止场景里一盏灯都没有时变全黑）
        glm::vec3 lightDir = { -0.2f, -1.0f, -0.3f }; 
        glm::vec3 lightColor = { 1.0f, 1.0f, 1.0f };
        float ambientStrength = 0.3f;

        // 在注册表里寻找所有同时拥有 Transform 和 DirectionalLight 的实体
        auto lightGroup = m_ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();
        for (auto entityID : lightGroup) {
            Entity lightEntity{ entityID, m_ActiveScene.get() };
            auto& transform = lightEntity.GetComponent<TransformComponent>();
            auto& dlc = lightEntity.GetComponent<DirectionalLightComponent>();

            // 数学魔法：将实体的欧拉角旋转 (Rotation) 转换成一个向前的方向向量！
            // 假设光的默认发射方向是沿着 -Z 轴，通过乘以实体的旋转四元数，得出真实方向。
            glm::quat orientation = glm::quat(transform.Rotation);
            lightDir = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
            
            lightColor = dlc.Color;
            ambientStrength = dlc.AmbientStrength;
            
            break; // 目前我们的 Shader 只支持一盏主平行光，找到第一个就跳出循环
        }
        // 设置一盏平行光（太阳光），从右前上方斜射下来
        lightingShader->SetFloat3("u_LightDir", lightDir); 
        lightingShader->SetFloat3("u_LightColor", lightColor);
        lightingShader->SetFloat("u_AmbientStrength", ambientStrength);


        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
        glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);

        // ==========================================
        // 渲染 3D 网格组件 (Mesh Renderer)
        // ==========================================
        auto meshGroup = m_ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshGroup) {
            Entity entity{ entityID, m_ActiveScene.get() };
            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            
            // 处理描边遮罩
            if (m_HoveredEntity && m_HoveredEntity == entity) {
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glStencilMask(0xFF); 
            } else {
                glStencilFunc(GL_ALWAYS, 0, 0xFF);
                glStencilMask(0x00); 
            }

            // 绑定贴图
            if (meshComp.TextureHandle != 0 && AssetManager::IsAssetHandleValid(meshComp.TextureHandle)) {
                auto tex = AssetManager::GetAsset<Texture2D>(meshComp.TextureHandle);
                tex->Bind(0);
            } else {
                m_WhiteTexture->Bind(0);
            }

            // defaultShader->SetFloat3("u_ColorModifier", glm::vec3(meshComp.Color)); 
            lightingShader->SetFloat3("u_ColorModifier", glm::vec3(meshComp.Color)); 
            
            // 提交网格渲染！
            if (meshComp.MeshGeometry) {
                Renderer::Submit(lightingShader, meshComp.MeshGeometry->GetVertexArray(), entity.GetWorldTransform());
            }
        }

        // ==========================================
        // 核心修复：描边判断也换成 MeshRendererComponent
        // ==========================================
        if (m_HoveredEntity && m_HoveredEntity.HasComponent<MeshRendererComponent>()) {
            glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
            glStencilMask(0x00);      
            glDisable(GL_DEPTH_TEST); 

            auto outlineShader = m_ShaderLibrary.Get("outline");
            outlineShader->Bind();
            outlineShader->SetFloat3("u_Color", glm::vec3(1.0f, 0.65f, 0.0f)); 
            
            glm::mat4 transform = m_HoveredEntity.GetWorldTransform();
            transform = transform * glm::scale(glm::mat4(1.0f), glm::vec3(1.05f)); 
            
            // 提交模型也要获取它真正的几何体，而不是用写死的 m_VertexArray
            auto& meshComp = m_HoveredEntity.GetComponent<MeshRendererComponent>();
            if (meshComp.MeshGeometry) {
                Renderer::Submit(outlineShader, meshComp.MeshGeometry->GetVertexArray(), transform);
            }

            glStencilMask(0xFF);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
        }

        Renderer::EndScene();
    }

    void EditorLayer::UIRenderDockspace() {
        static bool dockspaceOpen = true;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Ayaya Editor DockSpace", &dockspaceOpen, window_flags);
        ImGui::PopStyleVar(3);

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            static bool first_time = true;
            if (first_time) {
                first_time = false;
                ImGui::DockBuilderRemoveNode(dockspace_id); 
                ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

                ImGuiID dock_main_id = dockspace_id;
                ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
                ImGuiID dock_id_right_bottom = ImGui::DockBuilderSplitNode(dock_id_right, ImGuiDir_Down, 0.5f, nullptr, &dock_id_right);
                ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.30f, NULL, &dock_main_id);

                ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
                ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_id_right);
                ImGui::DockBuilderDockWindow("Properties", dock_id_right_bottom);
                ImGui::DockBuilderDockWindow("Content Browser", dock_id_bottom);
                
                ImGui::DockBuilderFinish(dockspace_id);
            }
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }
    }

    void EditorLayer::UIRenderMenuBar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                
                // ==========================================
                // 1. 新建场景 (New Scene)
                // ==========================================
                if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                    NewScene();
                }

                // ==========================================
                // 2. 保存与读取 (同步更新 m_CurrentScenePath)
                // ==========================================
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                    SaveScene();
                }
                
                // 新增：另存为按钮
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                    SaveSceneAs();
                }
                
                if (ImGui::MenuItem("Load Scene", "Ctrl+O")) {
                    OpenScene();
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) Application::Get().Close(); 
                
                ImGui::EndMenu();
            }

            // ==========================================
            // 新增 View 菜单
            // ==========================================
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Grid", nullptr, &m_ShowGrid);
                ImGui::EndMenu();
            }

            // ==========================================
            // 3. 在菜单栏最右侧显示当前场景名称！
            // ==========================================
            std::string sceneName = "Untitled";
            if (!m_CurrentScenePath.empty()) {
                // 从完整路径中提取文件名 (例如 /Users/xxx/assets/level.ayaya -> level.ayaya)
                size_t pos = m_CurrentScenePath.find_last_of("/\\");
                sceneName = pos != std::string::npos ? m_CurrentScenePath.substr(pos + 1) : m_CurrentScenePath;
            }
            std::string displayTitle = "Scene: " + sceneName;
            
            // 计算这段文字的宽度
            float textWidth = ImGui::CalcTextSize(displayTitle.c_str()).x;
            // 将鼠标光标强行推到菜单栏的最右侧
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - textWidth - 20.0f);
            // 使用系统禁用的暗淡颜色渲染文字，看起来更专业
            ImGui::TextDisabled("%s", displayTitle.c_str());

            ImGui::EndMenuBar();
        }

        
    }

    void EditorLayer::UIRenderViewport() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        ImGui::Begin("Viewport");

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
        auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
        auto viewportOffset = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
        m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        
        // ==========================================
        // 核心修复 1：这里绝对不能调用 m_Framebuffer->Resize!
        // 只静默更新 m_ViewportSize 供下一帧使用
        // ==========================================
        m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        ImGui::Image(reinterpret_cast<void*>((intptr_t)textureID), 
                     ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, 
                     ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        HandleMousePicking(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection());
        HandleGizmo(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection());

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::HandleMousePicking(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix) {
        m_HoveredEntity = {}; 
        
        if (!m_ViewportHovered || ImGuizmo::IsOver()) return;

        ImVec2 mousePos = ImGui::GetMousePos();
        float mx = mousePos.x - m_ViewportBounds[0].x;
        float my = mousePos.y - m_ViewportBounds[0].y;
        float viewportWidth = m_ViewportBounds[1].x - m_ViewportBounds[0].x;
        float viewportHeight = m_ViewportBounds[1].y - m_ViewportBounds[0].y;

        if (mx >= 0 && mx <= viewportWidth && my >= 0 && my <= viewportHeight) {
            my = viewportHeight - my; 
            float nx = (mx / viewportWidth) * 2.0f - 1.0f;
            float ny = (my / viewportHeight) * 2.0f - 1.0f;

            glm::vec4 clipCoords = glm::vec4(nx, ny, -1.0f, 1.0f);
            glm::vec4 eyeCoords = glm::inverse(cameraProjectionMatrix) * clipCoords;
            eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);
            glm::vec3 rayWorldDir = glm::normalize(glm::vec3(glm::inverse(cameraViewMatrix) * eyeCoords));
            glm::vec3 rayOrigin = glm::vec3(glm::inverse(cameraViewMatrix)[3]);

            float closestT = std::numeric_limits<float>::max();
            // ==========================================
            // 核心修复：把 SpriteRendererComponent 换成 MeshRendererComponent！
            // ==========================================
            auto renderGroup = m_ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();

            for (auto entityID : renderGroup) {
                Entity entity{ entityID, m_ActiveScene.get() };
                glm::mat4 inverseTransform = glm::inverse(entity.GetWorldTransform());

                glm::vec3 localRayOrigin = glm::vec3(inverseTransform * glm::vec4(rayOrigin, 1.0f));
                glm::vec3 localRayDir = glm::normalize(glm::vec3(inverseTransform * glm::vec4(rayWorldDir, 0.0f)));

                glm::vec3 invDir = 1.0f / localRayDir;
                glm::vec3 t0 = (-0.5f - localRayOrigin) * invDir; 
                glm::vec3 t1 = (0.5f - localRayOrigin) * invDir;

                glm::vec3 tmin = glm::min(t0, t1);
                glm::vec3 tmax = glm::max(t0, t1);

                float tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
                float tFar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);

                if (tNear <= tFar && tFar >= 0.0f) {
                    if (tNear < closestT) {
                        closestT = tNear;
                        m_HoveredEntity = entity;
                    }
                }
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_SceneHierarchyPanel.SetSelectedEntity(m_HoveredEntity);
            }
        }
    }

    void EditorLayer::HandleGizmo(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix) {
        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (!selectedEntity || m_GizmoType == -1) return;

        ImGuizmo::BeginFrame(); 
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();

        ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y, 
                          m_ViewportBounds[1].x - m_ViewportBounds[0].x, 
                          m_ViewportBounds[1].y - m_ViewportBounds[0].y);

        auto& tc = selectedEntity.GetComponent<TransformComponent>();
        glm::mat4 transform = selectedEntity.GetWorldTransform();

        bool snap = Input::IsKeyPressed(Key::LeftControl);
        float snapValue = (m_GizmoType == ImGuizmo::OPERATION::ROTATE) ? 45.0f : 0.5f; 
        float snapValues[3] = { snapValue, snapValue, snapValue };

        ImGuizmo::Manipulate(glm::value_ptr(cameraViewMatrix), glm::value_ptr(cameraProjectionMatrix),
            (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform),
            nullptr, snap ? snapValues : nullptr);

        if (ImGuizmo::IsUsing()) {
            auto& rel = selectedEntity.GetComponent<RelationshipComponent>();
            glm::mat4 localTransform = transform;
            
            if (rel.Parent != entt::null) {
                Entity parent{ rel.Parent, m_ActiveScene.get() };
                glm::mat4 parentWorld = parent.GetWorldTransform();
                localTransform = glm::inverse(parentWorld) * transform;
            }

            glm::vec3 scale, translation, skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose(localTransform, scale, rotation, translation, skew, perspective);
            
            tc.Translation = translation;
            tc.Rotation = glm::eulerAngles(rotation);
            tc.Scale = scale;
        }
    }
}