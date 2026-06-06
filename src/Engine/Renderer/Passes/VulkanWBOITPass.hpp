#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    // WBOIT Gather pass push constants (must match wboit_gather.vert/frag)
    struct alignas(16) WBOITGatherPushConstants {
        glm::mat4 Transform;
        glm::vec4 Albedo;
        float Metallic;
        float Roughness;
        float AO;
        int UseAlbedoMap;
        int UseMetallicMap;
        int UseRoughnessMap;
        int UseAOMap;
        int UseNormalMap;
        float Alpha;
    };

    // WBOIT (Weighted Blended Order-Independent Transparency)
    // Two-pass approach:
    //   1. Gather — all transparent objects rendered unordered into accumulation buffers
    //   2. Resolve — full-screen composite of accumulation onto SceneColor_HDR

    class VulkanWBOITPass {
    public:
        VulkanWBOITPass() = default;
        ~VulkanWBOITPass() = default;

        void OnAttach();
        void OnResize(uint32_t width, uint32_t height);

        // RenderGraph resource declarations
        static void DeclareGatherResources(class RGBuilder& builder,
                                           uint32_t width, uint32_t height);
        static void DeclareResolveResources(class RGBuilder& builder,
                                            uint32_t width, uint32_t height);

        // Execute callbacks
        void ExecuteGather(RenderContext& context, RenderCommandBuffer& cmd);
        void ExecuteResolve(RenderContext& context, RenderCommandBuffer& cmd);

    private:
        // Gather pass
        std::shared_ptr<Shader>    m_GatherShader;
        std::shared_ptr<Pipeline>  m_GatherPipeline;
        std::shared_ptr<Framebuffer> m_GatherRefFBO;
        PipelineSpecification m_GatherSpec;

        // Resolve pass (full-screen quad onto Lighting HDR)
        std::shared_ptr<Shader>    m_ResolveShader;
        std::shared_ptr<Pipeline>  m_ResolvePipeline;
        std::shared_ptr<Framebuffer> m_ResolveRefFBO;
        PipelineSpecification m_ResolveSpec;
    };

}
