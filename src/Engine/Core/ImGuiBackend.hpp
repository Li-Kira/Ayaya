#pragma once

namespace Ayaya {
    
    // 专门用于处理底层 ImGui 与系统窗口交互的静态工具类
    class ImGuiBackend {
    public:
        // 开启一个全新的 ImGui 帧
        static void BeginFrame();
        
        // 渲染 ImGui 数据、处理多视口 (Viewports) 并交换缓冲
        static void EndFrameAndSwapBuffers();
    };

}