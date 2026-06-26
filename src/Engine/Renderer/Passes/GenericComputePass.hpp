#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <memory>

namespace Ayaya {

    // Compute-only pass driven entirely by Lua parameters.
    // Supports texture-based dynamic dispatch (reads reference texture dimensions at Execute).
    class GenericComputePass : public RenderPass {
    public:
        GenericComputePass() { m_PassName = "GenericCompute"; }
        ~GenericComputePass() override;

        void OnAttach() override;
        void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t w, uint32_t h,
                                      const struct PassBakedParams& params);
        void SetNodeName(const std::string& name) { m_NodeName = name; }

    private:
        std::string m_NodeName;
        std::unordered_map<std::string, VkPipeline> m_PipelineCache;
        std::unordered_map<std::string, VkPipelineLayout> m_LayoutCache;
        std::unordered_map<std::string, VkShaderModule> m_ShaderCache;
    };

} // namespace Ayaya
