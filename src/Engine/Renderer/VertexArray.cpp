#include "ayapch.h"
#include "VertexArray.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLVertexArray.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    std::shared_ptr<VertexArray> VertexArray::Create() {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLVertexArray>();
            case RendererAPI::API::Vulkan:  
                AYAYA_CORE_ERROR("Vulkan VertexArray is under construction!"); 
                return nullptr;
            case RendererAPI::API::Metal:
                AYAYA_CORE_ERROR("Metal VertexArray is under construction!"); 
                return nullptr;
        }

        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}