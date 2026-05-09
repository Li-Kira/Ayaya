#pragma once
#include "Renderer/Texture.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Shader.hpp"
#include <memory>

namespace Ayaya {

    class VulkanIBLBuilder {
    public:
        static void* ConvertEquirectangularToCubemap(const std::shared_ptr<Texture2D>& hdrTexture, 
                                                        const std::shared_ptr<Mesh>& cubeMesh, 
                                                        const std::shared_ptr<Shader>& convertShader);

        static void* CreateIrradianceMap(void* envCubemap, 
                                            const std::shared_ptr<Mesh>& cubeMesh, 
                                            const std::shared_ptr<Shader>& irradianceShader);
        
        static void* CreatePrefilterMap(void* envCubemap, 
                                           const std::shared_ptr<Mesh>& cubeMesh, 
                                           const std::shared_ptr<Shader>& prefilterShader);
        
        static void* CreateBRDFLUT(const std::shared_ptr<Shader>& brdfShader, void* emptyVAO);

        // 在 VulkanIBLBuilder 类中新增：
        static void ClearResources();
        static void SetSourceCubemapSampler(void* sampler);
    private:
        static VkSampler s_SourceCubemapSampler;
    };

}