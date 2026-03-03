#pragma once

#include <Ayaya.hpp>
#include <Renderer/Renderer.hpp>
#include <Renderer/CameraController.hpp>
#include <Renderer/Shader.hpp>
#include <Renderer/Texture.hpp>
#include <Renderer/Framebuffer.hpp> // 引入 Framebuffer
#include <Events/ApplicationEvent.hpp>
#include <Events/KeyEvent.hpp>
#include <Events/MouseEvent.hpp>

// 必须包含 imgui 才能使用 OnImGuiRender
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>

class ExampleLayer : public Ayaya::Layer {
public:
    ExampleLayer() : Layer("ExampleLayer") {}

    virtual void OnAttach() override {
        // 1. 初始化相机控制器
        m_CameraController = std::make_unique<Ayaya::CameraController>(16.0f / 9.0f, true);

        // 2. 初始化 Framebuffer，将渲染画面传入 ImGui 的 Viewport 里面
        Ayaya::FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_Framebuffer = Ayaya::Framebuffer::Create(fbSpec);

        // 3. 加载资源并存入 ShaderLibrary
        m_ShaderLibrary.Load("assets/shaders/default.vert", "assets/shaders/default.frag");
        
        // 加载贴图 (确保路径正确)
        m_Texture = Ayaya::Texture2D::Create("assets/textures/bricks2.jpg");

        // 4. 构建几何体 (包含 UV 坐标)
        m_VertexArray.reset(Ayaya::VertexArray::Create());

        float vertices[] = {
            // Position(3f), Color(3f), TexCoord(2f)
            -0.5f, -0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  0.0f, 0.0f,
             0.5f, -0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  1.0f, 0.0f,
             0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  1.0f, 1.0f,
            -0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  0.0f, 1.0f,

            -0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 1.0f,  1.0f, 0.0f,
             0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 1.0f,  0.0f, 0.0f,
             0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 1.0f,  0.0f, 1.0f,
            -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 1.0f,  1.1f, 1.0f
        };

        auto vbo = std::shared_ptr<Ayaya::VertexBuffer>(Ayaya::VertexBuffer::Create(vertices, sizeof(vertices)));
        vbo->SetLayout({
            { Ayaya::ShaderDataType::Float3, "a_Position" },
            { Ayaya::ShaderDataType::Float3, "a_Color"    },
            { Ayaya::ShaderDataType::Float2, "a_TexCoord" }
        });
        m_VertexArray->AddVertexBuffer(vbo);

        uint32_t indices[] = {
            0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
            4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4
        };
        auto ibo = std::shared_ptr<Ayaya::IndexBuffer>(Ayaya::IndexBuffer::Create(indices, 36));
        m_VertexArray->SetIndexBuffer(ibo);
    }

    virtual void OnUpdate(Ayaya::Timestep ts) override {
        // 更新相机逻辑
        m_CameraController->OnUpdate(ts);

        // --- 1. 绑定 Framebuffer ---
        m_Framebuffer->Bind();

        // 清空 Framebuffer 的背景色 (这是你 3D 场景的背景色)
        Ayaya::RenderCommand::SetClearColor({ 0.12f, 0.12f, 0.14f, 1.0f });
        Ayaya::RenderCommand::Clear();

        Ayaya::Renderer::BeginScene(*m_CameraController);

        auto shader = m_ShaderLibrary.Get("default");
        m_Texture->Bind(0);
        shader->Bind();
        shader->SetInt("u_Texture", 0);
        shader->SetFloat3("u_ColorModifier", m_SquareColor);

        for (int x = 0; x < m_CubeCount; x++) {
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_SquareBasePos + glm::vec3(x * 1.5f, 0.0f, 0.0f));
            Ayaya::Renderer::Submit(shader, m_VertexArray, transform);
        }

        Ayaya::Renderer::EndScene();

        // --- 2. 解绑 Framebuffer ---
        m_Framebuffer->Unbind();

        // --- 3. 必须：清空系统主屏幕！---
        // 不清空的话，ImGui 面板背后会是一片漆黑或者残留着垃圾数据
        Ayaya::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f }); // 深灰色主窗口背景
        Ayaya::RenderCommand::Clear();
    }

    // --- 核心：可视化调试界面 ---
    virtual void OnImGuiRender() override {
        // =========================================================
        // 1. 初始化全屏的 DockSpace 容器
        // =========================================================
        static bool dockspaceOpen = true;
        static bool opt_fullscreen = true;
        static bool opt_padding = false;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        // 设置全屏窗口的标志：没有标题栏、不能移动、不能改变大小等
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen) {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        // 移除主容器的内边距
        if (!opt_padding)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        // 开始绘制主容器
        ImGui::Begin("Ayaya Editor DockSpace", &dockspaceOpen, window_flags);

        if (!opt_padding) ImGui::PopStyleVar();
        if (opt_fullscreen) ImGui::PopStyleVar(2);

        // 提交 DockSpace 节点
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            
            // =========================================================
            // 新增：自动构建默认布局
            // =========================================================
            static bool first_time = true;
            if (first_time) {
                first_time = false;
                
                // 只有当尚未存在停靠节点时（通常是没有 imgui.ini 文件时），才执行初始化布局
                if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
                    // 清理并重新添加主停靠节点
                    ImGui::DockBuilderRemoveNode(dockspace_id); 
                    ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
                    // 设置主节点大小为当前主视口大小
                    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

                    // --- 开始切分屏幕 ---
                    ImGuiID dock_main_id = dockspace_id;
                    // 把主节点向右切分，分出 25% 的宽度给 Debugger，剩下的保留给主节点
                    ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);

                    // --- 绑定窗口到对应的区域 ---
                    // 注意：这里的字符串必须和你后面 ImGui::Begin("名字") 里的名字一模一样！
                    ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
                    ImGui::DockBuilderDockWindow("Ayaya Engine Debugger", dock_id_right);
                    
                    // 结束布局构建
                    ImGui::DockBuilderFinish(dockspace_id);
                }
            }

            // 正式绘制 DockSpace
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        // =========================================================
        // 2. 顶部菜单栏 (增加专业感)
        // =========================================================
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) {
                    Ayaya::Application::Get().GetWindow().ShouldClose(); // 注意：这里可能需要你在 Application/Window 里暴露一个 Close 方法
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // =========================================================
        // 3. 原有的面板 (它们现在会自动吸附到 DockSpace 里)
        // =========================================================
        
        // 侧边栏调试面板
        ImGui::Begin("Ayaya Engine Debugger");
        ImGui::ColorEdit3("Global Color Tint", glm::value_ptr(m_SquareColor));
        ImGui::DragFloat3("Base Position", glm::value_ptr(m_SquareBasePos), 0.05f);
        ImGui::SliderInt("Cube Count", &m_CubeCount, 1, 10);
        ImGui::End();

        // 核心 Viewport 视口窗口
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        ImGui::Begin("Viewport");

        // --- 新增：获取 Viewport 的焦点状态，并同步给相机 ---
        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();
        m_CameraController->SetActive(m_ViewportFocused);
        // ---------------------------------------------------

        // 获取可用的面板大小
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        
        if (m_ViewportSize.x != viewportPanelSize.x || m_ViewportSize.y != viewportPanelSize.y) {
            if (viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f) {
                m_Framebuffer->Resize((uint32_t)viewportPanelSize.x, (uint32_t)viewportPanelSize.y);
                m_CameraController->OnResize(viewportPanelSize.x, viewportPanelSize.y);
                m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
            }
        }

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        ImGui::Image(reinterpret_cast<void*>((intptr_t)textureID), 
                     ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, 
                     ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        ImGui::End();
        ImGui::PopStyleVar(); // 恢复 Viewport 的内边距样式

        // =========================================================
        // 4. 结束主容器
        // =========================================================
        ImGui::End(); // 结束 "Ayaya Editor DockSpace"
    }

    virtual void OnEvent(Ayaya::Event& event) override {
        Ayaya::EventDispatcher dispatcher(event);
        
        // 之前我们在窗口 Resize 时让相机也 Resize
        // 但现在画面在 Viewport 里，相机的 Resize 已经在 OnImGuiRender 里处理了
        // 这里暂时保留，或者直接返回 false 即可
        dispatcher.Dispatch<Ayaya::WindowResizeEvent>([this](Ayaya::WindowResizeEvent& e) {
            return false; 
        });
    }

private:
    Ayaya::ShaderLibrary m_ShaderLibrary; 
    std::shared_ptr<Ayaya::Texture2D> m_Texture;
    std::shared_ptr<Ayaya::VertexArray> m_VertexArray;
    std::unique_ptr<Ayaya::CameraController> m_CameraController;

    std::shared_ptr<Ayaya::Framebuffer> m_Framebuffer; // Framebuffer 实例
    glm::vec2 m_ViewportSize = { 0.0f, 0.0f };         // 记录当前视口尺寸

    // 可通过 ImGui 调节的数据成员
    glm::vec3 m_SquareBasePos = { 0.0f, 0.0f, 0.0f };
    glm::vec3 m_SquareColor = { 1.0f, 1.0f, 1.0f };
    int m_CubeCount = 3;

    bool m_ViewportFocused = false;
    bool m_ViewportHovered = false;
};