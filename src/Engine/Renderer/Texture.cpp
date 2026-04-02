#include "ayapch.h"
#include "Texture.hpp"

#include "Renderer/RendererAPI.hpp" // 改为直接获取底层 API
#include "Platform/OpenGL/OpenGLTexture2D.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    std::shared_ptr<Texture2D> Texture2D::Create(const std::string& path) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(path);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan Texture2D is under construction!"); return nullptr;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal Texture2D is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Texture2D> Texture2D::Create(uint32_t width, uint32_t height) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(width, height);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan Texture2D is under construction!"); return nullptr;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal Texture2D is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Texture2D> Texture2D::Create(uint32_t rendererID, uint32_t width, uint32_t height) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(rendererID, width, height);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan Texture2D is under construction!"); return nullptr;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal Texture2D is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }
}