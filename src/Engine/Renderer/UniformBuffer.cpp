#include "ayapch.h"
#include "UniformBuffer.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLUniformBuffer.hpp"
#include "Platform/Vulkan/VulkanUniformBuffer.hpp" 
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
                // 返回 Vulkan 实例
                return std::make_shared<VulkanUniformBuffer>(size, binding); 
            case RendererAPI::API::Metal:
                AYAYA_CORE_ERROR("Metal UniformBuffer is under construction!"); 
                return nullptr;
        }

        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }
}