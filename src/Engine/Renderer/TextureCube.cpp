#include "ayapch.h"
#include "TextureCube.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLTextureCube.hpp"
#include "Platform/Vulkan/VulkanTextureCube.hpp"
#include "Core/Log.hpp"
#include "Core/VFS.hpp" // 【新增】：引入 VFS 虚拟文件系统

#include <algorithm> // 用于 std::replace

namespace Ayaya {

    std::shared_ptr<TextureCube> TextureCube::Create(const std::vector<std::string>& faces) {
        
        // ==========================================
        // 【核心防御与 VFS 改造】：VFS 路径解析 + 路径清洗器
        // 1. 将传入的虚拟路径 (project:// 或 engine://) 解析为电脑上的绝对物理路径
        // 2. 自动将所有 Windows 风格的反斜杠 '\' 替换为跨平台通用的正斜杠 '/'
        // ==========================================
        std::vector<std::string> sanitizedFaces;
        sanitizedFaces.reserve(faces.size());

        for (const auto& path : faces) {
            // 【新增】：调用 VFS 拿到真实的硬盘物理路径
            std::string physicalPath = VFS::ResolveString(path);
            
            // 继续执行原有的路径清洗
            std::replace(physicalPath.begin(), physicalPath.end(), '\\', '/');
            
            sanitizedFaces.push_back(physicalPath);
        }

        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
            case RendererAPI::API::OpenGL:  
                // 此时传给底层 OpenGL 的，已经是彻彻底底的物理硬盘路径了！
                return std::make_shared<OpenGLTextureCube>(sanitizedFaces);
            case RendererAPI::API::Vulkan:  
                return std::make_shared<VulkanTextureCube>(sanitizedFaces);
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