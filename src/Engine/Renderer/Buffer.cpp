#include "ayapch.h"
#include "Buffer.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Platform/OpenGL/OpenGLBuffer.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    std::shared_ptr<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLVertexBuffer>(vertices, size);
            case RendererAPI::API::Vulkan:  
                AYAYA_CORE_ERROR("Vulkan VertexBuffer is under construction!"); 
                return nullptr;
            case RendererAPI::API::Metal:
                AYAYA_CORE_ERROR("Metal VertexBuffer is under construction!"); 
                return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLIndexBuffer>(indices, count);
            case RendererAPI::API::Vulkan:  
                AYAYA_CORE_ERROR("Vulkan IndexBuffer is under construction!"); 
                return nullptr;
            case RendererAPI::API::Metal:
                AYAYA_CORE_ERROR("Metal IndexBuffer is under construction!"); 
                return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}