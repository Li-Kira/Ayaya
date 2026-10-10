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

        // 运行时 API override（游戏导出用）：在 Application 构造前调用，让 LoadConfig 跳过
        // EditorPreferences.yaml，改用 game.json 指定的后端。
        static void SetGraphicsAPIOverride(RendererAPI::API api);

        inline static void SetClearColor(const glm::vec4& color) { s_RendererAPI->SetClearColor(color); }
        inline static void Clear() { s_RendererAPI->Clear(); }
        inline static void DrawIndexed(const std::shared_ptr<VertexArray>& va) { s_RendererAPI->DrawIndexed(va); }

    private:
        // 注意这里：不再有 s_API，因为状态交还给 RendererAPI 管理了
        static std::unique_ptr<RendererAPI> s_RendererAPI;
        static bool s_APIOverrideSet;
    };
}