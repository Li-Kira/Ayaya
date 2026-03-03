#pragma once

#include "RenderCommand.hpp"
#include "CameraController.hpp"
#include "Shader.hpp"

namespace Ayaya {

    class Renderer {
    public:
        // 初始化渲染器（比如设置 OpenGL 状态）
        static void Init();
        // 清理渲染器资源
        static void Shutdown(); 
        
        // 窗口大小改变时通知渲染器
        static void OnWindowResize(uint32_t width, uint32_t height);

        // 开始场景：上传相机矩阵
        static void BeginScene(CameraController& cameraController);
        static void BeginScene(const glm::mat4& viewProjection);
        // 结束场景：目前预留，未来可用于后期处理
        static void EndScene();

        // 核心：提交渲染任务
        static void Submit(const std::shared_ptr<Shader>& shader, 
                           const std::shared_ptr<VertexArray>& vertexArray, 
                           const glm::mat4& transform = glm::mat4(1.0f));

        inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }

    private:
        // 存储每一帧都需要共享的数据（如投影视图矩阵）
        struct SceneData {
            glm::mat4 ViewProjectionMatrix;
        };

        static SceneData* m_SceneData;
    };

}