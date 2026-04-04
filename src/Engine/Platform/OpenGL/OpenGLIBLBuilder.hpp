#pragma once
#include "Renderer/Texture.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Shader.hpp"
#include <memory>

namespace Ayaya {

    class OpenGLIBLBuilder {
    public:
        static uint32_t ConvertEquirectangularToCubemap(const std::shared_ptr<Texture2D>& hdrTexture, 
                                                        const std::shared_ptr<Mesh>& cubeMesh, 
                                                        const std::shared_ptr<Shader>& convertShader);

        static uint32_t CreateIrradianceMap(uint32_t envCubemap, 
                                            const std::shared_ptr<Mesh>& cubeMesh, 
                                            const std::shared_ptr<Shader>& irradianceShader);
        
        static uint32_t CreatePrefilterMap(uint32_t envCubemap, 
                                           const std::shared_ptr<Mesh>& cubeMesh, 
                                           const std::shared_ptr<Shader>& prefilterShader);
        
        static uint32_t CreateBRDFLUT(const std::shared_ptr<Shader>& brdfShader, uint32_t emptyVAO);
    };

}