#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    class VulkanSSRPass : public RenderPass {
    public:
        VulkanSSRPass();
        ~VulkanSSRPass() override;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        static void DeclareResources(class RGBuilder& builder,
                                     uint32_t width, uint32_t height);

        static std::shared_ptr<Texture2D> GetBlueNoiseTexture();
        static void ReleaseBlueNoiseTexture();

        void SetHiZSource(class VulkanGBufferPass* gbuffer) { m_GBufferPass = gbuffer; }
        void SetUseHiZ(bool use) { m_UseHiZ = use; }
        bool IsUsingHiZ() const { return m_UseHiZ; }

    private:
        // March pipeline (fullscreen triangle ray march)
        std::shared_ptr<Shader>      m_MarchShader;
        std::shared_ptr<Pipeline>    m_MarchPipeline;
        std::shared_ptr<Framebuffer> m_RefFBO;         // 1280×720 ref for pipeline creation

        // Internal half-res FBO (SSR_Result is RenderGraph-managed)
        std::shared_ptr<Framebuffer> m_ReflectionFBO;
        uint32_t m_LastW = 0, m_LastH = 0;

        class VulkanGBufferPass* m_GBufferPass = nullptr;
        bool m_UseHiZ = true;  // toggle for A/B comparison (Hi-Z vs linear march)

        // Hi-Z resources for accelerated ray march
        VkDescriptorSetLayout m_HiZSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_HiZPool       = VK_NULL_HANDLE;
        VkDescriptorSet       m_HiZSets[3]    = {};
    };

} // namespace Ayaya
