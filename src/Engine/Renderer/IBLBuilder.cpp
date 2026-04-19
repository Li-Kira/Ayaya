#include "ayapch.h"
#include "IBLBuilder.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLIBLBuilder.hpp"
#include "Platform/Vulkan/VulkanIBLBuilder.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    void* IBLBuilder::ConvertEquirectangularToCubemap(const std::shared_ptr<Texture2D>& hdrTexture, 
                                                         const std::shared_ptr<Mesh>& cubeMesh, 
                                                         const std::shared_ptr<Shader>& convertShader) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return 0;
            case RendererAPI::API::OpenGL:  return OpenGLIBLBuilder::ConvertEquirectangularToCubemap(hdrTexture, cubeMesh, convertShader);
            case RendererAPI::API::Vulkan:  return VulkanIBLBuilder::ConvertEquirectangularToCubemap(hdrTexture, cubeMesh, convertShader);
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal IBLBuilder is under construction!"); return 0;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    void* IBLBuilder::CreateIrradianceMap(void* envCubemap, 
                                             const std::shared_ptr<Mesh>& cubeMesh, 
                                             const std::shared_ptr<Shader>& irradianceShader) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return 0;
            case RendererAPI::API::OpenGL:  return OpenGLIBLBuilder::CreateIrradianceMap(envCubemap, cubeMesh, irradianceShader);
            case RendererAPI::API::Vulkan:  return VulkanIBLBuilder::CreateIrradianceMap(envCubemap, cubeMesh, irradianceShader);
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal IBLBuilder is under construction!"); return 0;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    void* IBLBuilder::CreatePrefilterMap(void* envCubemap, 
                                            const std::shared_ptr<Mesh>& cubeMesh, 
                                            const std::shared_ptr<Shader>& prefilterShader) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return 0;
            case RendererAPI::API::OpenGL:  return OpenGLIBLBuilder::CreatePrefilterMap(envCubemap, cubeMesh, prefilterShader);
            case RendererAPI::API::Vulkan:  return VulkanIBLBuilder::CreatePrefilterMap(envCubemap, cubeMesh, prefilterShader);
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal IBLBuilder is under construction!"); return 0;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    void* IBLBuilder::CreateBRDFLUT(const std::shared_ptr<Shader>& brdfShader, void* emptyVAO) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return 0;
            case RendererAPI::API::OpenGL:  return OpenGLIBLBuilder::CreateBRDFLUT(brdfShader, emptyVAO);
            case RendererAPI::API::Vulkan:  return VulkanIBLBuilder::CreateBRDFLUT(brdfShader, emptyVAO);
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal IBLBuilder is under construction!"); return 0;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return 0;
    }

}