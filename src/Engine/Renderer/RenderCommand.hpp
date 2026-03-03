#pragma once

#include "RendererAPI.hpp"

namespace Ayaya {

    class RenderCommand {
    public:
        // 初始化渲染底层状态（如深度测试、混合等）
        inline static void Init() {
            s_RendererAPI->Init();
        }

        // 设置视口区域
        inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
            s_RendererAPI->SetViewport(x, y, width, height);
        }

        // 设置清屏颜色
        inline static void SetClearColor(const glm::vec4& color) {
            s_RendererAPI->SetClearColor(color);
        }

        // 执行清屏
        inline static void Clear() {
            s_RendererAPI->Clear();
        }

        // 执行索引绘图
        inline static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) {
            s_RendererAPI->DrawIndexed(vertexArray);
        }

    private:
        static RendererAPI* s_RendererAPI;
    };

}