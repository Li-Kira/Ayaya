#pragma once

#include "Renderer/RenderGraph.hpp"
#include "Renderer/PassRegistry.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

// sol2 types used in method signatures — must be fully defined
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace Ayaya {

    // ==========================================
    // PipelineBuilder — Lua → RenderGraph bridge
    //
    // Lifecycle:
    //   1. Lua script calls DeclareTexture / AddPass / SetOutput during setup (ONCE per compile)
    //   2. PipelineBuilder bakes all Lua params into pure C++ PassBakedParams
    //   3. Compile() calls m_Graph.Compile() — Kahn sort + FBO creation
    //   4. m_Graph.Execute() runs per-frame with ZERO Lua calls in the hot path
    //
    // Usage from SceneRenderer:
    //   m_PipelineBuilder.SetViewportSize(w, h);
    //   luaState["Pipeline"] = &m_PipelineBuilder;
    //   luaState.script_file(scriptPath);
    //   luaState["Pipeline"] = sol::nil;
    //   m_PipelineBuilder.Compile();
    // ==========================================
    class PipelineBuilder {
    public:
        explicit PipelineBuilder(RenderGraph& graph);

        // Per-renderer GDRContext for GenericDrawPass instances created during SRP compilation.
        void SetGDRContext(std::shared_ptr<struct GDRContext> ctx) { m_GDRCtx = std::move(ctx); }

        // Register pass instances for this renderer (called before Lua script execution).
        // Each SceneRenderer has its own pass instances with per-renderer pipelines/UBO bindings.
        void RegisterPassInstance(const std::string& name, std::shared_ptr<RenderPass> pass,
                                  std::shared_ptr<class VulkanWBOITPass> wboitPass = nullptr);

        // === Called from Lua DSL (setup phase only) ===

        void SetViewportSize(uint32_t w, uint32_t h);
        uint32_t GetViewportWidth() const;
        uint32_t GetViewportHeight() const;

        // Global shader parameters — accessible to all passes via RenderContext.
        // Lua: Pipeline:SetGlobalFloat("WindStrength", 2.5)
        void SetGlobalFloat(const std::string& name, float value);
        void SetGlobalInt(const std::string& name, int value);

        // Declare a named render target.
        //   name        — unique texture name (e.g., "GBuffer")
        //   formatList  — sol::table of format strings: {"RGBA8", "Depth"}
        //   width       — absolute pixels (0 = use viewport width)
        //   height      — absolute pixels (0 = use viewport height)
        //   samples     — MSAA sample count (default 1)
        //   isShadowMap — enable hardware PCF shadow sampler
        void DeclareTexture(const std::string& name, sol::table formatList,
                            uint32_t width, uint32_t height,
                            uint32_t samples = 1, bool isShadowMap = false);

        // Add a pass node to the graph.
        //   nodeName   — unique DAG node name (e.g., "MainGBuffer")
        //   passType   — registered factory name (e.g., "GBufferPass")
        //   reads      — sol table of texture name strings
        //   writes     — sol table of texture name strings
        //   readWrites — sol table of texture name strings (ReadWriteTexture, NOT Read+Write!)
        //   params     — optional sol table for per-pass config:
        //                  { Enabled = false, LightMode = "GBuffer", Queue = "Opaque", ... }
        //   widthOverride  — override viewport width (0 = use viewport)
        //   heightOverride — override viewport height (0 = use viewport)
        //
        // CRITICAL: params are BAKED into PassBakedParams during this call.
        // The execute_lambda captures ONLY pure C++ data — ZERO Lua calls per frame.
        void AddPass(const std::string& nodeName, const std::string& passType,
                     sol::table reads, sol::table writes, sol::table readWrites,
                     sol::object params,
                     uint32_t widthOverride = 0, uint32_t heightOverride = 0);

        // Set the final output texture name (for editor viewport display).
        void SetOutput(const std::string& textureName);

        // Validate texture usage after all passes are declared (warns about unused textures).
        void ValidateTextures();

        // Compile the DAG — Kahn topological sort + physical FBO creation.
        void Compile();

        // === Post-compile accessors (pure C++, no Lua) ===

        RenderGraph& GetGraph() { return m_Graph; }
        const std::string& GetOutput() const { return m_Output; }
        const PassBakedParams* GetBakedParams(const std::string& nodeName) const;

    private:
        RenderGraph& m_Graph;
        uint32_t m_ViewportWidth = 1280;
        uint32_t m_ViewportHeight = 720;
        std::string m_Output;

        // Declared textures (name → spec) — populated by DeclareTexture, referenced by AddPass
        std::unordered_map<std::string, FramebufferSpecification> m_TextureSpecs;

        // Pass instances — registered per-renderer before Lua execution
        std::unordered_map<std::string, std::shared_ptr<RenderPass>> m_PassInstances;
        std::shared_ptr<class VulkanWBOITPass> m_WBOITPass;  // special: ExecuteGather/ExecuteResolve
        std::shared_ptr<struct GDRContext> m_GDRCtx;     // per-renderer, for GenericDrawPass creation

        // BAKED parameters — pure C++, zero Lua dependency in hot path
        std::unordered_map<std::string, PassBakedParams> m_BakedParams;

        // Validation: track pass names and texture usage
        std::unordered_set<std::string> m_PassNames;      // duplicate detection
        std::unordered_set<std::string> m_UsedTextures;    // unused texture detection

        // === Internal helpers ===

        // Extract a sol::table into PassBakedParams (called ONCE during AddPass).
        static PassBakedParams BakeParams(sol::object params);

        // Convert a Lua array-of-strings table to vector<string>.
        static std::vector<std::string> LuaTableToStringVector(sol::table t);

        // Map format name string to FramebufferTextureFormat enum.
        static FramebufferTextureFormat ParseFormat(const std::string& name);

        // Resolve a previously declared texture's FramebufferSpecification.
        FramebufferSpecification GetSpecForTexture(const std::string& name) const;
    };

} // namespace Ayaya
