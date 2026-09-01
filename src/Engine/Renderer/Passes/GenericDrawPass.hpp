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

        // Scene input descriptor set (Set 3, graphics stage):
        //   binding 0 = SceneColor, 1 = SceneDepth, 2 = PrefilterMap (IBL), 3 = BRDFLUT
        VkDescriptorSetLayout m_SceneInputLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_SceneInputPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_SceneInputDescriptors;

        // Graphics pipeline cache: keyed by (shader, depth, cull, blend, format, depthFunc, colorWrite)
        struct PipelineKey {
            std::string shader;
            bool depthTest = true, depthWrite = true;
            int cullMode = 0;   // 0=None, 1=Front, 2=Back
            int blendMode = 0;  // 0=Opaque, 1=Additive, 2=AlphaBlend
            FramebufferTextureFormat colorFormat = FramebufferTextureFormat::RGBA16F;
            bool hasDepth = false;
            int  depthFunc = 0;   // 0=LESS(default), 1=LEQUAL
            bool colorWrite = true;  // false → writeMask=0 for all attachments (depth-only)
            bool operator==(const PipelineKey& o) const {
                return shader == o.shader && depthTest == o.depthTest &&
                       depthWrite == o.depthWrite && cullMode == o.cullMode &&
                       blendMode == o.blendMode && colorFormat == o.colorFormat &&
                       hasDepth == o.hasDepth && depthFunc == o.depthFunc &&
                       colorWrite == o.colorWrite;
            }
        };
        struct PipelineKeyHash {
            static inline void hash_combine(size_t& seed, size_t h) {
                seed ^= h + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            size_t operator()(const PipelineKey& k) const {
                size_t s = 0;
                hash_combine(s, std::hash<std::string>{}(k.shader));
                hash_combine(s, std::hash<bool>{}(k.depthTest));
                hash_combine(s, std::hash<bool>{}(k.depthWrite));
                hash_combine(s, std::hash<int>{}(k.cullMode));
                hash_combine(s, std::hash<int>{}(k.blendMode));
                hash_combine(s, std::hash<int>{}(static_cast<int>(k.colorFormat)));
                hash_combine(s, std::hash<bool>{}(k.hasDepth));
                hash_combine(s, std::hash<int>{}(k.depthFunc));
                hash_combine(s, std::hash<bool>{}(k.colorWrite));
                return s;
            }
        };
        std::unordered_map<PipelineKey, std::shared_ptr<Pipeline>, PipelineKeyHash> m_PipelineCache;

        std::shared_ptr<Shader> LoadShader(const std::string& vertPath, const std::string& fragPath);
        std::shared_ptr<Pipeline> GetOrCreatePipeline(const PipelineKey& key, const FramebufferSpecification& fboSpec);
        void BindSceneInputs(RenderContext& context, VkCommandBuffer vkCmd, uint32_t frameIdx,
                             VkPipelineLayout layout, bool color, bool depth, bool ibl);
    };

} // namespace Ayaya
