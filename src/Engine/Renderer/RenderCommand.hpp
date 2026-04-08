#pragma once

#include "RendererAPI.hpp"
#include <memory>

namespace Ayaya {

    class RenderCommand {
    public:
        // 【核心修改】：把 Init() 的具体实现移到 cpp 文件中！
        static void Init();

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
        // 【核心修改】：升级为智能指针，防止内存泄漏，且默认不要在头文件里 new！
        static std::unique_ptr<RendererAPI> s_RendererAPI;
    };

}