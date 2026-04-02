#include "ayapch.h"
#include "UniformBuffer.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Platform/OpenGL/OpenGLUniformBuffer.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    std::shared_ptr<UniformBuffer> UniformBuffer::Create(uint32_t size, uint32_t binding) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLUniformBuffer>(size, binding);
            case RendererAPI::API::Vulkan:  
                AYAYA_CORE_ERROR("Vulkan UniformBuffer is under construction!"); 
                return nullptr;
            case RendererAPI::API::Metal:
                AYAYA_CORE_ERROR("Metal UniformBuffer is under construction!"); 
                return nullptr;
        }

        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}