#include "PassRegistry.hpp"
#include "Renderer/Passes/GenericDrawPass.hpp"
#include "Renderer/Passes/GenericFullScreenPass.hpp"
#include "Renderer/Passes/GenericComputePass.hpp"
#include "Renderer/Passes/VulkanShadowPass.hpp"
#include "Renderer/Passes/VulkanGbufferPass.hpp"
#include "Renderer/Passes/VulkanDepthPrePass.hpp"
#include "Renderer/Passes/VulkanLightingPass.hpp"
#include "Renderer/Passes/VulkanForwardBlendPass.hpp"
#include "Renderer/Passes/VulkanSSAOPass.hpp"
#include "Renderer/Passes/VulkanSSRPass.hpp"
#include "Renderer/Passes/VulkanApplyReflectionPass.hpp"
#include "Renderer/Passes/VulkanOutlinePass.hpp"
#include "Renderer/Passes/VulkanBloomPass.hpp"
#include "Renderer/Passes/VulkanPostProcessPass.hpp"
#include "Renderer/Passes/VulkanFXAAPass.hpp"
#include "Renderer/Passes/UIPass.hpp"
#include "Renderer/Passes/VulkanWBOITPass.hpp"
#include "Renderer/GDRContext.hpp"
#include "Renderer/SceneRenderer.hpp"

#include <algorithm>

namespace Ayaya {

    // ==========================================
    // Static storage
    // ==========================================
    std::unordered_map<std::string, PassRegistry::FactoryFn> PassRegistry::s_Factories;
    std::unordered_map<std::string, std::unique_ptr<IPassFactory>> PassRegistry::s_Instances;

    // ==========================================
    // Template factory for standard RenderPass subclasses
    //
    // DeclareFn signature: void(RGBuilder&) or void(RGBuilder&, uint32_t, uint32_t)
    // Execute: calls pass->Execute(ctx, cmd)
    // ==========================================
    template<typename TPass, typename DeclareFn>
    class StandardPassFactory : public IPassFactory {
    public:
        StandardPassFactory(DeclareFn declareFn, std::shared_ptr<TPass> pass)
            : m_DeclareFn(std::move(declareFn)), m_Pass(std::move(pass)) {}

        void DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height,
                              const PassBakedParams& /*params*/) override {
            if constexpr (std::is_invocable_v<DeclareFn, RGBuilder&>) {
                m_DeclareFn(builder);
            } else {
                m_DeclareFn(builder, width, height);
            }
        }

        RGExecuteFn GetExecuteFn() override {
            auto pass = m_Pass;
            return [pass](RenderContext& ctx, RenderCommandBuffer& cmd) {
                if (pass) pass->Execute(ctx, cmd);
            };
        }

    private:
        DeclareFn m_DeclareFn;
        std::shared_ptr<TPass> m_Pass;
    };

    // ==========================================
    // WBOIT Gather factory — calls DeclareGatherResources + ExecuteGather
    // ==========================================
    class WBOITGatherFactory : public IPassFactory {
    public:
        explicit WBOITGatherFactory(std::shared_ptr<VulkanWBOITPass> pass)
            : m_Pass(std::move(pass)) {}

        void DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height,
                              const PassBakedParams& /*params*/) override {
            VulkanWBOITPass::DeclareGatherResources(builder, width, height);
        }

        RGExecuteFn GetExecuteFn() override {
            auto pass = m_Pass;
            return [pass](RenderContext& ctx, RenderCommandBuffer& cmd) {
                if (pass) pass->ExecuteGather(ctx, cmd);
            };
        }

    private:
        std::shared_ptr<VulkanWBOITPass> m_Pass;
    };

    // ==========================================
    // WBOIT Resolve factory — calls DeclareResolveResources + ExecuteResolve
    // ==========================================
    class WBOITResolveFactory : public IPassFactory {
    public:
        explicit WBOITResolveFactory(std::shared_ptr<VulkanWBOITPass> pass)
            : m_Pass(std::move(pass)) {}

        void DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height,
                              const PassBakedParams& /*params*/) override {
            VulkanWBOITPass::DeclareResolveResources(builder, width, height);
        }

        RGExecuteFn GetExecuteFn() override {
            auto pass = m_Pass;
            return [pass](RenderContext& ctx, RenderCommandBuffer& cmd) {
                if (pass) pass->ExecuteResolve(ctx, cmd);
            };
        }

    private:
        std::shared_ptr<VulkanWBOITPass> m_Pass;
    };

    // ==========================================
    // LightingPass factory — wraps DeclareResources + extra ReadTexture("SSAO_Final")
    // ==========================================
    class LightingPassFactory : public IPassFactory {
    public:
        explicit LightingPassFactory(std::shared_ptr<RenderPass> pass)
            : m_Pass(std::move(pass)) {}

        void DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height,
                              const PassBakedParams& /*params*/) override {
            VulkanLightingPass::DeclareResources(builder, width, height);
            builder.ReadTexture("SSAO_Final");  // soft runtime dependency
        }

        RGExecuteFn GetExecuteFn() override {
            auto pass = m_Pass;
            return [pass](RenderContext& ctx, RenderCommandBuffer& cmd) {
                if (pass) pass->Execute(ctx, cmd);
            };
        }

    private:
        std::shared_ptr<RenderPass> m_Pass;
    };

    // ==========================================
    // PassRegistry static methods
    // ==========================================

    void PassRegistry::Init(std::shared_ptr<GDRContext> gdrCtx,
                            std::shared_ptr<RenderPass> shadowPass,
                            std::shared_ptr<RenderPass> gbufferPass,
                            std::shared_ptr<RenderPass> depthPrePass,
                            std::shared_ptr<RenderPass> lightingPass,
                            std::shared_ptr<RenderPass> forwardBlendPass,
                            std::shared_ptr<RenderPass> ssaoPass,
                            std::shared_ptr<RenderPass> ssrPass,
                            std::shared_ptr<RenderPass> applyReflectionPass,
                            std::shared_ptr<RenderPass> outlinePass,
                            std::shared_ptr<RenderPass> bloomPass,
                            std::shared_ptr<RenderPass> postProcessPass,
                            std::shared_ptr<RenderPass> fxaaPass,
                            std::shared_ptr<RenderPass> uiPass,
                            std::shared_ptr<class VulkanWBOITPass> wboitPass) {

        // Inject GDRContext into Depth, Shadow and GBuffer passes
        if (gdrCtx) {
            if (auto* depth = dynamic_cast<VulkanDepthPrePass*>(depthPrePass.get()))
                depth->SetGDRContext(gdrCtx);
            if (auto* shadow = dynamic_cast<VulkanShadowPass*>(shadowPass.get()))
                shadow->SetGDRContext(gdrCtx);
            if (auto* gbuffer = dynamic_cast<VulkanGBufferPass*>(gbufferPass.get()))
                gbuffer->SetGDRContext(gdrCtx);
        }

        // ── Register all 12 passes ──

        // DepthPrePass — depth-only (no color attachments)
        Register("DepthPrePass", [depthPrePass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanDepthPrePass::DeclareResources, depthPrePass);
        });

        // ShadowPass — no width/height needed (fixed 4096x4096)
        Register("ShadowPass", [shadowPass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanShadowPass::DeclareResources, shadowPass);
        });

        // GBufferPass
        Register("GBufferPass", [gbufferPass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanGBufferPass::DeclareResources, gbufferPass);
        });

        // SSAOPass
        Register("SSAOPass", [ssaoPass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanSSAOPass::DeclareResources, ssaoPass);
        });

        // SSRPass
        Register("SSRPass", [ssrPass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanSSRPass::DeclareResources, ssrPass);
        });

        // ApplyReflection — UE-style hierarchical SSR/IBL specular replacement
        Register("ApplyReflection", [applyReflectionPass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanApplyReflectionPass::DeclareResources, applyReflectionPass);
        });

        // LightingPass — special: extra ReadTexture("SSAO_Final")
        Register("LightingPass", [lightingPass]() -> std::unique_ptr<IPassFactory> {
            return std::make_unique<LightingPassFactory>(lightingPass);
        });

        // ForwardBlend
        Register("ForwardBlend", [forwardBlendPass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanForwardBlendPass::DeclareResources, forwardBlendPass);
        });

        // WBOIT_Gather — uses VulkanWBOITPass but different Declare + Execute
        Register("WBOIT_Gather", [wboitPass]() -> std::unique_ptr<IPassFactory> {
            return std::make_unique<WBOITGatherFactory>(wboitPass);
        });

        // WBOIT_Resolve
        Register("WBOIT_Resolve", [wboitPass]() -> std::unique_ptr<IPassFactory> {
            return std::make_unique<WBOITResolveFactory>(wboitPass);
        });

        // OutlinePass
        Register("OutlinePass", [outlinePass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanOutlinePass::DeclareResources, outlinePass);
        });

        // BloomPass
        Register("BloomPass", [bloomPass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanBloomPass::DeclareResources, bloomPass);
        });

        // PostProcessPass
        Register("PostProcessPass", [postProcessPass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanPostProcessPass::DeclareResources, postProcessPass);
        });

        // FXAAPass
        Register("FXAAPass", [fxaaPass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                VulkanFXAAPass::DeclareResources, fxaaPass);
        });

        // UIPass
        Register("UIPass", [uiPass]() -> std::unique_ptr<IPassFactory> {
            using Fn = void(*)(RGBuilder&, uint32_t, uint32_t);
            return std::make_unique<StandardPassFactory<RenderPass, Fn>>(
                UIPass::DeclareResources, uiPass);
        });

        // ── Generic passes (Parameter-driven, zero-C++ for custom effects) ──
        Register("GenericDrawPass", []() -> std::unique_ptr<IPassFactory> {
            struct Factory : public IPassFactory {
                std::shared_ptr<RenderPass> pass = std::make_shared<GenericDrawPass>();
                void DeclareResources(RGBuilder&, uint32_t, uint32_t, const PassBakedParams&) override {}
                RGExecuteFn GetExecuteFn() override {
                    auto p = pass;
                    return [p](RenderContext& ctx, RenderCommandBuffer& cmd) { p->Execute(ctx, cmd); };
                }
            };
            auto f = std::make_unique<Factory>();
            // GDRContext is per-renderer — set by PipelineBuilder at SRP compile time
            f->pass->OnAttach();
            return f;
        });

        Register("GenericFullScreenPass", []() -> std::unique_ptr<IPassFactory> {
            struct Factory : public IPassFactory {
                std::shared_ptr<RenderPass> pass = std::make_shared<GenericFullScreenPass>();
                void DeclareResources(RGBuilder&, uint32_t, uint32_t, const PassBakedParams&) override {}
                RGExecuteFn GetExecuteFn() override {
                    auto p = pass;
                    return [p](RenderContext& ctx, RenderCommandBuffer& cmd) { p->Execute(ctx, cmd); };
                }
            };
            auto f = std::make_unique<Factory>();
            f->pass->OnAttach();
            return f;
        });

        Register("GenericComputePass", []() -> std::unique_ptr<IPassFactory> {
            struct Factory : public IPassFactory {
                std::shared_ptr<RenderPass> pass = std::make_shared<GenericComputePass>();
                void DeclareResources(RGBuilder&, uint32_t, uint32_t, const PassBakedParams&) override {}
                RGExecuteFn GetExecuteFn() override {
                    auto p = pass;
                    return [p](RenderContext& ctx, RenderCommandBuffer& cmd) { p->Execute(ctx, cmd); };
                }
            };
            auto f = std::make_unique<Factory>();
            f->pass->OnAttach();
            return f;
        });
    }

    void PassRegistry::Shutdown() {
        s_Instances.clear();
        s_Factories.clear();
    }

    void PassRegistry::Register(const std::string& name, FactoryFn factory) {
        s_Factories[name] = std::move(factory);
    }

    IPassFactory* PassRegistry::Get(const std::string& name) {
        // Check already-instantiated factories first
        auto itInst = s_Instances.find(name);
        if (itInst != s_Instances.end())
            return itInst->second.get();

        // Lazy-instantiate from factory function
        auto itFn = s_Factories.find(name);
        if (itFn == s_Factories.end())
            return nullptr;

        auto instance = itFn->second();
        auto* ptr = instance.get();
        s_Instances[name] = std::move(instance);
        return ptr;
    }

    std::vector<std::string> PassRegistry::GetRegisteredNames() {
        std::vector<std::string> names;
        names.reserve(s_Factories.size());
        for (auto& [name, _] : s_Factories)
            names.push_back(name);
        std::sort(names.begin(), names.end());
        return names;
    }

} // namespace Ayaya
