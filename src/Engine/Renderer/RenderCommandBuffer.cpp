#include "ayapch.h"
#include "RenderCommandBuffer.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLCommandBuffer.hpp"
// 【新增】：引入 Vulkan 实现
#include "Platform/Vulkan/VulkanRenderCommandBuffer.hpp"
#include "Core/Log.hpp" 

namespace Ayaya {

    std::shared_ptr<RenderCommandBuffer> RenderCommandBuffer::Create() {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
                
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLCommandBuffer>();
                
            case RendererAPI::API::Vulkan:  
                // 【核心修复】：返回合法的 Vulkan 实例对象
                return std::make_shared<VulkanRenderCommandBuffer>(); 
                
            case RendererAPI::API::Metal:
                AYAYA_CORE_ERROR("Metal CommandBuffer is under construction!"); 
                return nullptr;
        }

        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}