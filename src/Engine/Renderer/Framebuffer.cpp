#include "ayapch.h"
#include "Framebuffer.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Platform/OpenGL/OpenGLFramebuffer.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp" // 【新增】
#include "Core/Log.hpp"

namespace Ayaya {

    std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
                
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLFramebuffer>(spec);
                
            case RendererAPI::API::Vulkan:  
                return std::make_shared<VulkanFramebuffer>(spec);
                
            case RendererAPI::API::Metal:
                AYAYA_CORE_ERROR("Metal Framebuffer is under construction!"); 
                return nullptr;
        }

        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}