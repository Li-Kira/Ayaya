#include "ayapch.h"
#include "UniformBuffer.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLUniformBuffer.hpp"

namespace Ayaya {

    std::shared_ptr<UniformBuffer> UniformBuffer::Create(uint32_t size, uint32_t binding) {
        switch (Renderer::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_WARN("RendererAPI::None is currently not supported!"); 
                return nullptr;
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLUniformBuffer>(size, binding);
        }

        AYAYA_CORE_WARN("Unknown RendererAPI!");
        return nullptr;
    }

}