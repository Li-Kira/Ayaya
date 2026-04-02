#include "ayapch.h"
#include "TextureCube.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Platform/OpenGL/OpenGLTextureCube.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    std::shared_ptr<TextureCube> TextureCube::Create(const std::vector<std::string>& faces) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTextureCube>(faces);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan TextureCube is under construction!"); return nullptr;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal TextureCube is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<TextureCube> TextureCube::Create(uint32_t rendererID, int width, int height) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTextureCube>(rendererID, width, height);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan TextureCube is under construction!"); return nullptr;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal TextureCube is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}