#pragma once
#include "Renderer/Texture.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Shader.hpp"
#include <memory>

namespace Ayaya {
    class IBLBuilder {
    public:
        // 传入 2D 的 HDR 贴图，返回 OpenGL 底层 Cubemap 的 ID
        static void* ConvertEquirectangularToCubemap(const std::shared_ptr<Texture2D>& hdrTexture, 
                                                        const std::shared_ptr<Mesh>& cubeMesh, 
                                                        const std::shared_ptr<Shader>& convertShader);

        // 传入高分辨率的环境 Cubemap，返回模糊的 32x32 漫反射 Cubemap ID
        static void* CreateIrradianceMap(void* envCubemap, 
                                            const std::shared_ptr<Mesh>& cubeMesh, 
                                            const std::shared_ptr<Shader>& irradianceShader);
        
        static void* CreatePrefilterMap(void* envCubemap, const std::shared_ptr<Mesh>& cubeMesh, const std::shared_ptr<Shader>& prefilterShader);
        
        static void* CreateBRDFLUT(const std::shared_ptr<Shader>& brdfShader, void* emptyVAO);
    };
}