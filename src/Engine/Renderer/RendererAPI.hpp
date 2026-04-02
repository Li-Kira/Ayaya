#pragma once
#include <glm/glm.hpp>
#include <memory>
#include "Renderer/VertexArray.hpp"

namespace Ayaya {
    class RendererAPI {
    public:
        // 【核心拓展】：加入现代 API 支持
        enum class API { None = 0, OpenGL = 1, Vulkan = 2, Metal = 3 };

    public:
        virtual ~RendererAPI() = default;

        virtual void Init() = 0;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void SetClearColor(const glm::vec4& color) = 0;
        virtual void Clear() = 0;
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) = 0;

        inline static API GetAPI() { return s_API; }
        static void SetAPI(API api) { s_API = api; } // 允许引擎启动时设置 API

    private:
        static API s_API;
    };
}