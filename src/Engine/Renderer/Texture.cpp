#include "ayapch.h"
#include "Texture.hpp"
#include "Renderer/Renderer.hpp" 
#include "Platform/OpenGL/OpenGLTexture2D.hpp"
#include "Platform/Vulkan/VulkanTexture2D.hpp" 
#include "Core/Log.hpp"

namespace Ayaya {

    // ==========================================
    // 1. 从文件路径创建贴图
    // ==========================================
    std::shared_ptr<Texture2D> Texture2D::Create(const std::string& path) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(path);
            case RendererAPI::API::Vulkan:  return std::make_shared<VulkanTexture2D>(path); 
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal Texture2D is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    // ==========================================
    // 2. 指定宽高创建空贴图 (比如 WhiteTexture)
    // ==========================================
    std::shared_ptr<Texture2D> Texture2D::Create(uint32_t width, uint32_t height) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(width, height);
            case RendererAPI::API::Vulkan:  return std::make_shared<VulkanTexture2D>(width, height);
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal Texture2D is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }
    
    // ==========================================
    // 3. 从已有的底层 ID 包装贴图 (用于 IBL 和 FBO)
    // ==========================================
    std::shared_ptr<Texture2D> Texture2D::Create(void* rendererID, uint32_t width, uint32_t height) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(rendererID, width, height);
            case RendererAPI::API::Vulkan:  return std::make_shared<VulkanTexture2D>(rendererID, width, height);
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal Texture2D is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}