#pragma once
#include <memory>

namespace Ayaya {

    // 纯虚基类：不管底层是 OpenGL 还是 Vulkan，都要能初始化并交换缓冲
    class GraphicsContext {
    public:
        virtual ~GraphicsContext() = default;

        // 初始化底层图形 API (例如 gladLoadGLLoader 或 vkCreateInstance)
        virtual void Init() = 0;
        
        // 交换前后缓冲，将画面呈现 (Present) 到屏幕上
        virtual void SwapBuffers() = 0;

        // 静态工厂方法：接收 GLFW 窗口句柄
        static std::shared_ptr<GraphicsContext> Create(void* windowHandle);
    };

}