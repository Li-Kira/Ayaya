#include "ayapch.h"
#include "RenderCommand.hpp"
#include "Platform/OpenGL/OpenGLRendererAPI.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    // ==========================================
    // 1. 静态变量初始化为 nullptr，绝不在 main 之前抢跑！
    // ==========================================
    std::unique_ptr<RendererAPI> RenderCommand::s_RendererAPI = nullptr;

    // ==========================================
    // 2. 延迟加载 (Lazy Initialization)
    // ==========================================
    void RenderCommand::Init() {
        // 在这里，Application 已经读取完了 YAML，RendererAPI::GetAPI() 是绝对正确的！
        if (!s_RendererAPI) {
            switch (RendererAPI::GetAPI()) {
                case RendererAPI::API::None:
                    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!");
                    break;
                    
                case RendererAPI::API::OpenGL:
                    s_RendererAPI = std::make_unique<OpenGLRendererAPI>();
                    break;
                    
                case RendererAPI::API::Vulkan:
                    // s_RendererAPI = std::make_unique<VulkanRendererAPI>();
                    AYAYA_CORE_ERROR("VulkanRendererAPI is under construction!");
                    break;
                    
                case RendererAPI::API::Metal:
                    AYAYA_CORE_ERROR("MetalRendererAPI is under construction!");
                    break;
            }
        }

        if (s_RendererAPI) {
            s_RendererAPI->Init();
        }
    }

}