#include "ayapch.h"
#include "Texture.hpp"

#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLTexture2D.hpp"

namespace Ayaya {

    // 原来的那个通过路径创建贴图的函数
    std::shared_ptr<Texture2D> Texture2D::Create(const std::string& path) {
        switch (Renderer::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_WARN("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(path);
        }

        AYAYA_CORE_WARN("Unknown RendererAPI!");
        return nullptr;
    }

    // ==========================================================
    // --- 新增：实现我们刚才声明的、通过宽高创建贴图的函数 ---
    // ==========================================================
    std::shared_ptr<Texture2D> Texture2D::Create(uint32_t width, uint32_t height) {
        switch (Renderer::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_WARN("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(width, height);
        }

        AYAYA_CORE_WARN("Unknown RendererAPI!");
        return nullptr;
    }

}