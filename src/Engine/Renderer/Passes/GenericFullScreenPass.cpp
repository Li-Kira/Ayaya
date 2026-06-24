#include "ayapch.h"
#include "GenericFullScreenPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/PassRegistry.hpp"
#include "Renderer/Framebuffer.hpp"

namespace Ayaya {

    void GenericFullScreenPass::OnAttach() {
        // Nothing to pre-load — pipelines are created on-demand in GetOrCreatePipeline
    }

    void GenericFullScreenPass::DeclareResources(RGBuilder& builder, uint32_t w, uint32_t h,
                                                  const PassBakedParams& params) {
        // Explicit writes declared by PipelineBuilder from Lua
    }

    std::shared_ptr<Pipeline> GenericFullScreenPass::GetOrCreatePipeline(
        const std::string& fragShaderPath) {

        auto it = m_PipelineCache.find(fragShaderPath);
        if (it != m_PipelineCache.end()) return it->second;

        auto shader = Shader::Create("Generic/generic_fullscreen.vert", fragShaderPath);
        if (!shader) {
            AYAYA_CORE_ERROR("[GenericFullScreen] Failed to load shader: {}", fragShaderPath);
            return nullptr;
        }

        // Create a reference FBO for format matching (required by dynamic rendering)
        if (!m_RefFBO) {
            FramebufferSpecification refSpec;
            refSpec.Width = 1280; refSpec.Height = 720;
            refSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
            m_RefFBO = Framebuffer::Create(refSpec);
        }

        PipelineSpecification spec;
        spec.Shader = shader;
        spec.TargetFramebuffer = m_RefFBO;
        spec.DepthTest = false;
        spec.DepthWrite = false;
        spec.BackfaceCulling = CullMode::None;
        spec.Blend = false;
        spec.NoTextureDescriptors = false;

        auto pipeline = Pipeline::Create(spec);
        if (pipeline) m_PipelineCache[fragShaderPath] = pipeline;
        return pipeline;
    }

    void GenericFullScreenPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        // Use node name (from Lua) for param lookup; fall back to pass type name
        std::string prefix = m_NodeName.empty() ? m_PassName : m_NodeName;
        std::string fragShader = context.Get<std::string>(prefix + ".Shader", "");
        if (fragShader.empty()) {
            AYAYA_CORE_WARN("[GenericFullScreen] No Shader param set — skipping (node={})", prefix);
            return;
        }

        auto pipeline = GetOrCreatePipeline(fragShader);
        if (!pipeline) return;

        // Get the output FBO (declared as a write in Lua, injected by RenderGraph)
        std::string targetName = context.Get<std::string>(prefix + ".Target", "");
        if (targetName.empty()) targetName = "GrayscaleOut"; // fallback

        auto fbo = context.GetFramebuffer(targetName);
        if (!fbo) {
            AYAYA_CORE_WARN("[GenericFullScreen] Output FBO '{}' not found", targetName);
            return;
        }

        cmd.BeginRenderPass(fbo, true);

        cmd.BindPipeline(pipeline);

        // Bind input textures AFTER BindPipeline (which clears m_PendingImageInfos)
        for (int i = 0; i < 4; i++) {
            std::string key = prefix + ".Texture" + std::to_string(i);
            std::string texName = context.Get<std::string>(key, "");
            if (texName.empty()) continue;

            auto inFbo = context.GetFramebuffer(texName);
            if (inFbo) {
                std::string slotName = "u_Texture" + std::to_string(i);
                cmd.BindTexture2D(pipeline, slotName, i, inFbo, 0);
            }
        }

        cmd.DrawArrays(3);
        cmd.EndRenderPass();
    }

} // namespace Ayaya
