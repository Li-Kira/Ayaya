#pragma once

#include "RenderCommand.hpp"
#include "RendererAPI.hpp"

namespace Ayaya {

    // ==========================================
    // 现代架构下的 Renderer 类不再负责具体的 "Submit" 绘制。
    // 它是一个静态 RHI 调度器，负责全局图形硬件的生命周期管理。
    // 具体的绘制任务由 SceneRenderer 和 RenderCommandBuffer 接管！
    // ==========================================
    class Renderer {
    public:
        static void Init();
        static void Shutdown(); 
        static void OnWindowResize(uint32_t width, uint32_t height);

        // 预留给未来的全局帧生命周期调度
        // static void BeginFrame();
        // static void EndFrame();

        inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
    };

}