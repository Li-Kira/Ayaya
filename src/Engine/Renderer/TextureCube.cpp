#include "ayapch.h"
#include "TextureCube.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLTextureCube.hpp"
#include "Platform/Vulkan/VulkanTextureCube.hpp"
#include "Core/Log.hpp"

#include <algorithm> // 【新增】：用于 std::replace

namespace Ayaya {

    std::shared_ptr<TextureCube> TextureCube::Create(const std::vector<std::string>& faces) {
        
        // ==========================================
        // 【核心防御】：路径清洗器 (Path Sanitizer)
        // 自动将所有 Windows 风格的反斜杠 '\' 替换为跨平台通用的正斜杠 '/'
        // ==========================================
        std::vector<std::string> sanitizedFaces = faces;
        for (auto& path : sanitizedFaces) {
            std::replace(path.begin(), path.end(), '\\', '/');
        }

        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
            case RendererAPI::API::OpenGL:  
                // 【修改】：传入清洗后的 sanitizedFaces，而不是原始的 faces
                return std::make_shared<OpenGLTextureCube>(sanitizedFaces);
            case RendererAPI::API::Vulkan:  
                return std::make_shared<VulkanTextureCube>(sanitizedFaces);
                return nullptr;
            case RendererAPI::API::Metal:   
                AYAYA_CORE_ERROR("Metal TextureCube is under construction!"); 
                return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<TextureCube> TextureCube::Create(void* rendererID, int width, int height) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLTextureCube>(rendererID, width, height);
            case RendererAPI::API::Vulkan:  
                return std::make_shared<VulkanTextureCube>(rendererID, width, height);
            case RendererAPI::API::Metal:   
                AYAYA_CORE_ERROR("Metal TextureCube is under construction!"); 
                return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}