#include "ayapch.h"
#include "Renderer.hpp"

namespace Ayaya {

    RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;
    
    void Renderer::Init() {
        RenderCommand::Init();

        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            // glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS); 
        }
        else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            // 未来可以在这里进行 Vulkan 描述符池、全局管线缓存的初始化
            // AYAYA_CORE_INFO("Initializing Vulkan Global Resources...");
        }
    }

    void Renderer::Shutdown() {
        // 统一清理全局 RHI 资源
    }

    void Renderer::OnWindowResize(uint32_t width, uint32_t height) {
        RenderCommand::SetViewport(0, 0, width, height);
    }

    // 删除了所有的 BeginScene / EndScene / Submit !

}