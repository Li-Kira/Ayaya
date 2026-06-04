#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    struct alignas(16) FXAAPushConstants {
        glm::vec2 TexelSize;
    };

    class VulkanFXAAPass : public RenderPass {
    public:
        VulkanFXAAPass();
        virtual ~VulkanFXAAPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        std::shared_ptr<Shader> m_FXAAShader;
        std::shared_ptr<Framebuffer> m_FXAAFBO;
        std::shared_ptr<Pipeline> m_Pipeline;
    };

}
