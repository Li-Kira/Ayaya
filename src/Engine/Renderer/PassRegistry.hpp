#pragma once

#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderPipeline.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace Ayaya {

    class VulkanWBOITPass;
    struct GDRContext;

    // ==========================================
    // PassBakedParams — pure C++ parameter struct
    //
    // Extracted ONCE from Lua sol::table during PipelineBuilder::AddPass (setup phase).
    // execute_lambda captures this struct BY VALUE — zero Lua calls in the hot path.
    // ==========================================
    struct PassBakedParams {
        bool Enabled = true;
        std::string LightMode;   // e.g., "GBuffer", "ShadowCaster", "Forward"
        std::string Queue;       // e.g., "Opaque", "Translucent"

        std::unordered_map<std::string, float>       FloatParams;
        std::unordered_map<std::string, int>         IntParams;
        std::unordered_map<std::string, std::string> StrParams;

        bool HasKey(const std::string& key) const {
            return FloatParams.count(key) || IntParams.count(key) || StrParams.count(key);
        }
        float GetFloat(const std::string& key, float def = 0.0f) const {
            auto it = FloatParams.find(key);
            return it != FloatParams.end() ? it->second : def;
        }
        int GetInt(const std::string& key, int def = 0) const {
            auto it = IntParams.find(key);
            return it != IntParams.end() ? it->second : def;
        }
        const std::string& GetStr(const std::string& key) const {
            static const std::string kEmpty;
            auto it = StrParams.find(key);
            return it != StrParams.end() ? it->second : kEmpty;
        }
    };

    // ==========================================
    // IPassFactory — unified pass creation interface
    //
    // Each registered pass type provides one factory instance.
    // DeclareResources is called during graph compilation (NOT per-frame).
    // GetExecuteFn returns the per-frame callback (RenderPass::Execute or WBOIT::ExecuteGather/Resolve).
    // ==========================================
    class IPassFactory {
    public:
        virtual ~IPassFactory() = default;

        // Declare the pass's resource I/O into the RGBuilder.
        // width/height = viewport dimensions (0 for fixed-size passes like ShadowMap @ 4096).
        // params = already-baked C++ parameters (extracted from Lua by PipelineBuilder).
        virtual void DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height,
                                      const PassBakedParams& params) = 0;

        // Return the per-frame execute callback.
        // The returned function is called every frame from m_RenderGraph.Execute().
        virtual RGExecuteFn GetExecuteFn() = 0;
    };

    // ==========================================
    // PassRegistry — global pass factory registry
    //
    // Usage:
    //   PassRegistry::Init(gdrCtx);                          // register all 12 built-in passes
    //   auto* factory = PassRegistry::Get("GBufferPass");   // lookup by name
    // ==========================================
    class PassRegistry {
    public:
        // Register all 12 built-in passes. Must be called after SceneRenderer creates pass instances.
        // gdrCtx is injected into ShadowPass and GBufferPass factories.
        static void Init(std::shared_ptr<struct GDRContext> gdrCtx,
                         std::shared_ptr<RenderPass> shadowPass,
                         std::shared_ptr<RenderPass> gbufferPass,
                         std::shared_ptr<RenderPass> depthPrePass,
                         std::shared_ptr<RenderPass> lightingPass,
                         std::shared_ptr<RenderPass> forwardBlendPass,
                         std::shared_ptr<RenderPass> ssaoPass,
                         std::shared_ptr<RenderPass> outlinePass,
                         std::shared_ptr<RenderPass> bloomPass,
                         std::shared_ptr<RenderPass> postProcessPass,
                         std::shared_ptr<RenderPass> fxaaPass,
                         std::shared_ptr<RenderPass> uiPass,
                         std::shared_ptr<class VulkanWBOITPass> wboitPass);
        static void Shutdown();

        using FactoryFn = std::function<std::unique_ptr<IPassFactory>()>;

        // Register a pass factory by name. The factory is called once to create the IPassFactory instance.
        static void Register(const std::string& name, FactoryFn factory);

        // Look up a factory by registered name. Returns nullptr if not found.
        static IPassFactory* Get(const std::string& name);

        // Convenience: list all registered pass names.
        static std::vector<std::string> GetRegisteredNames();

    private:
        // Factories stored by name — each entry creates one IPassFactory on first use.
        static std::unordered_map<std::string, FactoryFn> s_Factories;
        // Instantiated factories, created on first Get() call.
        static std::unordered_map<std::string, std::unique_ptr<IPassFactory>> s_Instances;
    };

} // namespace Ayaya
