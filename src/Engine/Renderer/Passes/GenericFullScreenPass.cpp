#include "ayapch.h"
#include "GenericFullScreenPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/PassRegistry.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"

namespace Ayaya {

    GenericFullScreenPass::~GenericFullScreenPass() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();
        vkDeviceWaitIdle(device);
        m_PipelineCache.clear();
    }

    void GenericFullScreenPass::OnAttach() {
        // Nothing to pre-load — pipelines are created on-demand in GetOrCreatePipeline
    }

    void GenericFullScreenPass::DeclareResources(RGBuilder& builder, uint32_t w, uint32_t h,
                                                  const PassBakedParams& params) {
        // Explicit writes declared by PipelineBuilder from Lua
    }

    std::shared_ptr<Pipeline> GenericFullScreenPass::GetOrCreatePipeline(
        const std::string& fragShaderPath, const std::string& vertShaderPath,
        int blendMode, FramebufferTextureFormat targetFormat, bool hasDepth) {

        // Cache key includes format+depth so different targets get own pipelines
        std::string cacheKey = (vertShaderPath.empty() ? "DEFAULT_VERT" : vertShaderPath)
                             + "|" + fragShaderPath + "|blend=" + std::to_string(blendMode)
                             + "|fmt=" + std::to_string((int)targetFormat)
                             + (hasDepth ? "|D" : "");
        auto it = m_PipelineCache.find(cacheKey);
        if (it != m_PipelineCache.end()) return it->second;

        std::shared_ptr<Shader> shader;
        try {
            shader = Shader::Create(
                vertShaderPath.empty() ? "Generic/generic_fullscreen.vert" : vertShaderPath,
                fragShaderPath);
        } catch (const std::exception& e) {
            AYAYA_CORE_ERROR("[GenericFullScreen] Shader load exception for '{}': {}", fragShaderPath, e.what());
            return nullptr;
        }
        if (!shader) {
            AYAYA_CORE_ERROR("[GenericFullScreen] Failed to load shader: {} — check SPIR-V compilation", fragShaderPath);
            return nullptr;
        }

        // Reference FBO matching the runtime target (format + optional depth)
        FramebufferSpecification refSpec;
        refSpec.Width = 1280; refSpec.Height = 720;
        refSpec.Attachments = { targetFormat };
        if (hasDepth)
            refSpec.Attachments.Attachments.push_back({FramebufferTextureFormat::Depth});
        auto refFBO = Framebuffer::Create(refSpec);

        PipelineSpecification spec;
        spec.Shader = shader;
        spec.TargetFramebuffer = refFBO;
        spec.DepthTest = false;
        spec.DepthWrite = false;
        spec.BackfaceCulling = CullMode::None;
        // Blend mode: 0=Opaque(off), 1=Additive, 2=Alpha
        spec.Blend = (blendMode != 0);
        spec.BlendMode = (blendMode == 1) ? BlendModeType::Additive :
                         (blendMode == 2) ? BlendModeType::Alpha :
                         BlendModeType::None;
        spec.NoTextureDescriptors = false;

        std::shared_ptr<Pipeline> pipeline;
        try {
            pipeline = Pipeline::Create(spec);
        } catch (const std::exception& e) {
            AYAYA_CORE_ERROR("[GenericFullScreen] Pipeline creation failed for '{}': {} — check entry point or descriptor bindings", fragShaderPath, e.what());
            return nullptr;
        }
        if (pipeline) m_PipelineCache[cacheKey] = pipeline;
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

        std::string vertShader = context.Get<std::string>(prefix + ".VertexShader", "");
        int blendMode  = context.Get<int>(prefix + ".BlendMode", 0);
        bool clearColor = context.Get<int>(prefix + ".ClearColor", 1) != 0;

        // Get output FBO first — need its format for pipeline creation
        std::string targetName = context.Get<std::string>(prefix + ".Target", "");
        if (targetName.empty()) targetName = "GrayscaleOut";
        auto fbo = context.GetFramebuffer(targetName);
        if (!fbo) {
            AYAYA_CORE_WARN("[GenericFullScreen:'{}'] Output FBO '{}' not found", prefix, targetName);
            return;
        }

        // Extract color + depth format from runtime FBO (e.g. Lighting → RGBA16F+Depth)
        FramebufferTextureFormat fmt = FramebufferTextureFormat::RGBA8;
        bool hasDepth = false;
        for (auto& att : fbo->GetSpecification().Attachments.Attachments) {
            if (att.TextureFormat != FramebufferTextureFormat::Depth &&
                att.TextureFormat != FramebufferTextureFormat::DEPTH24STENCIL8) {
                fmt = att.TextureFormat;
            } else {
                hasDepth = true;
            }
        }

        auto pipeline = GetOrCreatePipeline(fragShader, vertShader, blendMode, fmt, hasDepth);
        if (!pipeline) {
            AYAYA_CORE_WARN("[GenericFullScreen:'{}'] Pipeline FAILED", prefix);
            return;
        }
        AYAYA_CORE_INFO("[GenericFullScreen:'{}'] target={} fmt={} depth={} blend={} clear={}",
            prefix, targetName, (int)fmt, hasDepth, blendMode, clearColor);

        cmd.BeginRenderPass(fbo, clearColor);

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
