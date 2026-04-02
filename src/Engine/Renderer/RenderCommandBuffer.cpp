#include "ayapch.h"
#include "RenderCommandBuffer.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Platform/OpenGL/OpenGLCommandBuffer.hpp"
#include "Core/Log.hpp" // 【新增】：确保引入了 Log 系统

namespace Ayaya {

    std::shared_ptr<RenderCommandBuffer> RenderCommandBuffer::Create() {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
                
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLCommandBuffer>();
                
            case RendererAPI::API::Vulkan:  
                AYAYA_CORE_ERROR("Vulkan CommandBuffer is under construction!"); 
                return nullptr;
                
            case RendererAPI::API::Metal:
                AYAYA_CORE_ERROR("Metal CommandBuffer is under construction!"); 
                return nullptr;
        }

        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}