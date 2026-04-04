#include "ayapch.h"
#include "IBLBuilder.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Platform/OpenGL/OpenGLIBLBuilder.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    uint32_t IBLBuilder::ConvertEquirectangularToCubemap(const std::shared_ptr<Texture2D>& hdrTexture, 
                                                         const std::shared_ptr<Mesh>& cubeMesh, 
                                                         const std::shared_ptr<Shader>& convertShader) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return 0;
            case RendererAPI::API::OpenGL:  return OpenGLIBLBuilder::ConvertEquirectangularToCubemap(hdrTexture, cubeMesh, convertShader);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan IBLBuilder is under construction!"); return 0;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal IBLBuilder is under construction!"); return 0;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return 0;
    }

    uint32_t IBLBuilder::CreateIrradianceMap(uint32_t envCubemap, 
                                             const std::shared_ptr<Mesh>& cubeMesh, 
                                             const std::shared_ptr<Shader>& irradianceShader) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return 0;
            case RendererAPI::API::OpenGL:  return OpenGLIBLBuilder::CreateIrradianceMap(envCubemap, cubeMesh, irradianceShader);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan IBLBuilder is under construction!"); return 0;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal IBLBuilder is under construction!"); return 0;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return 0;
    }

    uint32_t IBLBuilder::CreatePrefilterMap(uint32_t envCubemap, 
                                            const std::shared_ptr<Mesh>& cubeMesh, 
                                            const std::shared_ptr<Shader>& prefilterShader) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return 0;
            case RendererAPI::API::OpenGL:  return OpenGLIBLBuilder::CreatePrefilterMap(envCubemap, cubeMesh, prefilterShader);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan IBLBuilder is under construction!"); return 0;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal IBLBuilder is under construction!"); return 0;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return 0;
    }

    uint32_t IBLBuilder::CreateBRDFLUT(const std::shared_ptr<Shader>& brdfShader, uint32_t emptyVAO) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return 0;
            case RendererAPI::API::OpenGL:  return OpenGLIBLBuilder::CreateBRDFLUT(brdfShader, emptyVAO);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan IBLBuilder is under construction!"); return 0;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal IBLBuilder is under construction!"); return 0;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return 0;
    }

}