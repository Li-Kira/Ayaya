#pragma once

#include "Renderer/RendererAPI.hpp"

namespace Ayaya {

    class OpenGLRendererAPI : public RendererAPI {
    public:
        // 新增：初始化 OpenGL 状态（深度测试、混合等）
        virtual void Init() override;
        
        // 新增：封装 glViewport
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

        virtual void SetClearColor(const glm::vec4& color) override;
        virtual void Clear() override;

        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) override;
    };

}