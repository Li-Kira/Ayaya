#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Engine/Scene/Entity.hpp"

namespace Ayaya {

    struct alignas(16) GBufferPushConstants {
        glm::mat4 Transform;
        alignas(16) glm::vec3 Albedo;
        float ReceiveShadows;
        float Metallic;
        float Roughness;
        float AO;
        float AlphaMultiplier;
        float AlphaCutoff;
        int   BlendMode;       // 0=Opaque, 1=Masked
        int UseAlbedoMap;
        int UseNormalMap;
        int UseORMMap;        // [NEW] UE4-style ORM packed texture
        int UseMetallicMap;
        int UseRoughnessMap;
        int UseAOMap;
        int UseAlphaMap;
        int IsSelected;
    };

    class VulkanGBufferPass : public RenderPass {
    public:
        VulkanGBufferPass() { m_PassName = "G-Buffer Pass"; }
        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        std::shared_ptr<Shader> m_GBufferShader;
        std::shared_ptr<Framebuffer> m_RefFBO;  // format reference FBO
        PipelineSpecification m_PipeSpec;
        std::shared_ptr<Pipeline> m_Pipeline;
    };
}
