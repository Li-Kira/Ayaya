#include "Renderer.hpp"

namespace Ayaya {

    RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;

    void Renderer::Init() {
        RenderCommand::Init();

        // OpenGL 专属的特性设置，未来我们需要把这行移到 OpenGLRendererAPI::Init 里
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            // 消除高粗糙度 Cubemap Mipmap 采样时的可见缝隙
            // 注意：Vulkan 默认就是无缝的，不需要这个
            // glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS); 
        }
    }

    void Renderer::Shutdown() {
    }

    void Renderer::OnWindowResize(uint32_t width, uint32_t height) {
        RenderCommand::SetViewport(0, 0, width, height);
    }

    void Renderer::BeginScene(const glm::mat4& viewProjection) {
    }

    void Renderer::EndScene() {
    }

    void Renderer::Submit(const std::shared_ptr<Shader>& shader, 
                          const std::shared_ptr<VertexArray>& vertexArray, 
                          const glm::mat4& transform) {
        
        // 上传由物体自己管理的局部 Transform（Model 矩阵）
        // 因为每个物体位置不同，这个数据不是全局的，所以依然用 Uniform 传递
        shader->SetMat4("u_Transform", transform);

        // 绑定 VAO 并绘制
        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
    }
}