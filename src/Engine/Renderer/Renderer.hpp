#pragma once

#include "RendererAPI.hpp"
#include <memory>

namespace Ayaya {
    class Renderer {
    public:
        static void LoadConfig(); 
        static void Init();
        static void Shutdown(); 
        static void OnWindowResize(uint32_t width, uint32_t height);

        inline static void SetClearColor(const glm::vec4& color) { s_RendererAPI->SetClearColor(color); }
        inline static void Clear() { s_RendererAPI->Clear(); }
        inline static void DrawIndexed(const std::shared_ptr<VertexArray>& va) { s_RendererAPI->DrawIndexed(va); }

    private:
        // 注意这里：不再有 s_API，因为状态交还给 RendererAPI 管理了
        static std::unique_ptr<RendererAPI> s_RendererAPI;
    };
}