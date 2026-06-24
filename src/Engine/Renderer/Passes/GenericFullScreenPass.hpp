#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Pipeline.hpp"
#include "Renderer/Framebuffer.hpp"
#include <string>
#include <unordered_map>
#include <memory>

namespace Ayaya {

    // Full-screen post-processing pass driven entirely by Lua parameters.
    // Loads a fragment shader, binds input textures, draws a full-screen triangle.
    // Zero per-effect C++ code — TA controls everything via .srp Lua script.
    class GenericFullScreenPass : public RenderPass {
    public:
        GenericFullScreenPass() { m_PassName = "GenericFullScreen"; }
        ~GenericFullScreenPass() override = default;

        void OnAttach() override;
        void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        void SetNodeName(const std::string& name) { m_NodeName = name; }
        static void DeclareResources(class RGBuilder& builder, uint32_t w, uint32_t h,
                                      const struct PassBakedParams& params);

    private:
        std::string m_NodeName;
        std::shared_ptr<Framebuffer> m_RefFBO;  // reference FBO for pipeline format matching
        std::unordered_map<std::string, std::shared_ptr<Pipeline>> m_PipelineCache;

        std::shared_ptr<Pipeline> GetOrCreatePipeline(const std::string& fragShaderPath);
    };

} // namespace Ayaya
