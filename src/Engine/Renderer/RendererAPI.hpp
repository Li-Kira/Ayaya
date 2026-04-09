#pragma once
#include <glm/glm.hpp>
#include <memory>
#include "Renderer/VertexArray.hpp"

namespace Ayaya {
    // 纯粹的底层图形接口
    class RendererAPI {
    public:
        enum class API { None = 0, OpenGL = 1, Vulkan = 2, Metal = 3 };

        virtual ~RendererAPI() = default;

        virtual void Init() = 0;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void SetClearColor(const glm::vec4& color) = 0;
        virtual void Clear() = 0;
        
        // 仅保留极少数需要绕过 CommandBuffer 的全局直接绘制（如 ImGui 或特殊清屏）
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) = 0;

        inline static API GetAPI() { return s_API; }
        static void SetAPI(API api) { s_API = api; }
        
    private:
        static API s_API;
    };
}