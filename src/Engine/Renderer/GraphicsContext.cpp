#include "ayapch.h"
#include "GraphicsContext.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Platform/OpenGL/OpenGLContext.hpp"
#include "Platform/Vulkan/VulkanContext.hpp" // 我们马上创建它
#include "Core/Log.hpp"

struct GLFWwindow;

namespace Ayaya {

    std::shared_ptr<GraphicsContext> GraphicsContext::Create(void* windowHandle) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
                
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLContext>(static_cast<GLFWwindow*>(windowHandle));
                
            case RendererAPI::API::Vulkan:  
                return std::make_shared<VulkanContext>(static_cast<GLFWwindow*>(windowHandle));
                
            case RendererAPI::API::Metal:
                AYAYA_CORE_ERROR("Metal Context is under construction!"); 
                return nullptr;
        }

        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}