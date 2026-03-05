#include "Renderer.hpp"

namespace Ayaya {

    // 分配静态内存
    Renderer::SceneData* Renderer::m_SceneData = new Renderer::SceneData();

    // --- 新增：在这里定义并初始化 RendererAPI 的静态变量 ---
    RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;

    void Renderer::Init() {
        // 调用渲染指令代理，彻底去 OpenGL 化
        RenderCommand::Init();
    }

    void Renderer::Shutdown() {
        // 释放静态分配的场景数据，防止内存泄漏
        delete m_SceneData;
    }

    void Renderer::OnWindowResize(uint32_t width, uint32_t height) {
        // 转发视口调整指令
        RenderCommand::SetViewport(0, 0, width, height);
    }

    void Renderer::BeginScene(CameraController& cameraController) {
        // 从控制器中提取预计算好的 ViewProjection 矩阵
        m_SceneData->ViewProjectionMatrix = cameraController.GetViewProjection();
    }

    void Renderer::BeginScene(const glm::mat4& viewProjection) {
        m_SceneData->ViewProjectionMatrix = viewProjection;
    }

    void Renderer::EndScene() {
        // 场景渲染结束
    }

    void Renderer::Submit(const std::shared_ptr<Shader>& shader, 
                          const std::shared_ptr<VertexArray>& vertexArray, 
                          const glm::mat4& transform) {
        // 1. 绑定 Shader
        shader->Bind();
        
        // 2. 上传由 Renderer 统一管理的全局 Uniform
        shader->SetMat4("u_ViewProjection", m_SceneData->ViewProjectionMatrix);
        
        // 3. 上传由物体自己管理的局部 Transform（Model 矩阵）
        shader->SetMat4("u_Transform", transform);

        // 4. 绑定 VAO
        vertexArray->Bind();
        
        // 5. 调用渲染命令（分发到 OpenGLRendererAPI）
        RenderCommand::DrawIndexed(vertexArray);
    }

}