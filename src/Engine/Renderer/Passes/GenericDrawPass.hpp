#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Pipeline.hpp"
#include "Platform/Vulkan/VulkanStorageBuffer.hpp"
#include "Renderer/GDRContext.hpp"
#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace Ayaya {

    // Geometry rendering pass driven entirely by Lua parameters.
    // Uses GDR compute culling + indirect draw — TA controls shader,
    // LightMode mask, and pipeline state via .srp script.
    class GenericDrawPass : public RenderPass {
    public:
        GenericDrawPass() { m_PassName = "GenericDraw"; }
        ~GenericDrawPass() override;

        void OnAttach() override;
        void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t w, uint32_t h,
                                      const struct PassBakedParams& params);
        void SetGDRContext(std::shared_ptr<GDRContext> ctx) { m_GDRCtx = ctx; }
        void SetNodeName(const std::string& name) { m_NodeName = name; }

    private:
        void ExecuteOpaque(RenderContext& context, RenderCommandBuffer& cmd,
                           const std::string& prefix, uint32_t lightModeMask);
        void ExecuteTransparent(RenderContext& context, RenderCommandBuffer& cmd,
                                const std::string& prefix, uint32_t lightModeMask);

        std::string m_NodeName;
        std::shared_ptr<GDRContext> m_GDRCtx;

        // Compute culling resources
        VkPipelineLayout   m_CullLayout = VK_NULL_HANDLE;
        VkPipeline         m_CullPipeline = VK_NULL_HANDLE;
        VkShaderModule     m_CullShader = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CullDummyLayout = VK_NULL_HANDLE; // empty layout for set 0/1 placeholders
        VkDescriptorSetLayout m_CullSet3Layout = VK_NULL_HANDLE;
        VkDescriptorPool   m_CullSet3Pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_CullSet3Descriptors;
        std::unique_ptr<VulkanStorageBuffer> m_DrawIndirectBuffer;

        // Graphics pipeline cache: keyed by (shaderPath, depthTest, cullMode, blendMode, colorFormat, hasDepth)
        struct PipelineKey {
            std::string shader;
            bool depthTest = true, depthWrite = true;
            int cullMode = 0;   // 0=None, 1=Front, 2=Back
            int blendMode = 0;  // 0=Opaque, 1=Additive, 2=AlphaBlend
            FramebufferTextureFormat colorFormat = FramebufferTextureFormat::RGBA16F;
            bool hasDepth = false;  // does the runtime target have a depth attachment?
            bool operator==(const PipelineKey& o) const {
                return shader == o.shader && depthTest == o.depthTest &&
                       depthWrite == o.depthWrite && cullMode == o.cullMode &&
                       blendMode == o.blendMode && colorFormat == o.colorFormat &&
                       hasDepth == o.hasDepth;
            }
        };
        struct PipelineKeyHash {
            size_t operator()(const PipelineKey& k) const {
                return std::hash<std::string>{}(k.shader) ^
                       (k.depthTest ? 0x01 : 0) ^ (k.depthWrite ? 0x02 : 0) ^
                       (k.cullMode << 2) ^ (k.blendMode << 4) ^
                       ((int)k.colorFormat << 8) ^ (k.hasDepth ? 0x1000 : 0);
            }
        };
        std::unordered_map<PipelineKey, std::shared_ptr<Pipeline>, PipelineKeyHash> m_PipelineCache;

        std::shared_ptr<Shader> LoadShader(const std::string& vertPath, const std::string& fragPath);
        std::shared_ptr<Pipeline> GetOrCreatePipeline(const PipelineKey& key);
    };

} // namespace Ayaya
