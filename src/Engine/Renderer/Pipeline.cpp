#include "ayapch.h"
#include "Pipeline.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLPipeline.hpp"
// 【核心】：引入 Vulkan 管线的头文件
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    std::shared_ptr<Pipeline> Pipeline::Create(const PipelineSpecification& spec) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
                
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLPipeline>(spec);
                
            case RendererAPI::API::Vulkan:  
                // 返回我们在之前步骤里写好的 Vulkan 管线骨架
                return std::make_shared<VulkanPipeline>(spec);
                
            case RendererAPI::API::Metal:
                AYAYA_CORE_ERROR("Metal Pipeline is under construction!"); 
                return nullptr;
        }

        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}