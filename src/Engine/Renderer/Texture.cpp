#include "ayapch.h"
#include "Texture.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLTexture2D.hpp"
#include "Platform/Vulkan/VulkanTexture2D.hpp"
#include "Asset/AssetManager.hpp" 
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

    // ==========================================
    // 4. 异步加载：后台线程读取像素数据（纯 CPU，线程安全）
    // ==========================================
    RawTextureData Texture2D::LoadRawDataFromDisk(const std::string& path) {
        RawTextureData raw;
        raw.SourcePath = path;

        // 读取 .meta 导入设置，用 FlipY 控制 stb 翻转
        UUID handle = AssetManager::FindHandleForPath(path);
        if (handle != 0) raw.ImportSettings = AssetManager::GetMetadata(handle).TextureSettings;

        bool isHDR = stbi_is_hdr(path.c_str());
        raw.IsHDR = isHDR;

        stbi_set_flip_vertically_on_load(raw.ImportSettings.FlipY ? 1 : 0);

        int w, h, c;
        if (isHDR)
            raw.Pixels = stbi_loadf(path.c_str(), &w, &h, &c, STBI_rgb_alpha);
        else
            raw.Pixels = stbi_load(path.c_str(), &w, &h, &c, STBI_rgb_alpha);

        raw.Width = w; raw.Height = h; raw.Channels = c;

        if (!raw.Pixels)
            AYAYA_CORE_ERROR("Texture2D::LoadRawDataFromDisk failed: {0}", path);

        return raw;
    }

    // ==========================================
    // 5. 异步加载：主线程 GPU 上传
    // ==========================================
    std::shared_ptr<Texture2D> Texture2D::CreateFromRawData(const RawTextureData& raw) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(raw);
            case RendererAPI::API::Vulkan:  return std::make_shared<VulkanTexture2D>(raw);
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal Texture2D is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}