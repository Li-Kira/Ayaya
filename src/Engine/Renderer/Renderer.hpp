#pragma once

#include "RenderCommand.hpp"
#include "CameraController.hpp"
#include "Shader.hpp"

namespace Ayaya {

    class Renderer {
    public:
        static void Init();
        static void Shutdown(); 
        static void OnWindowResize(uint32_t width, uint32_t height);

        // BeginScene 和 EndScene 现在可以是空壳，或者留作未来拓展
        static void BeginScene(const glm::mat4& viewProjection);
        static void EndScene();

        // 核心：提交渲染任务
        static void Submit(const std::shared_ptr<Shader>& shader, 
                           const std::shared_ptr<VertexArray>& vertexArray, 
                           const glm::mat4& transform = glm::mat4(1.0f));

        inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
    };

}