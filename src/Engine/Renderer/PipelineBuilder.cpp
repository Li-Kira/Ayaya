#include "ayapch.h"
#include "PipelineBuilder.hpp"
#include "Renderer/Passes/VulkanWBOITPass.hpp"

// sol2 — for BakeParams / LuaTableToStringVector
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace Ayaya {

    // ==========================================
    // Format string → FramebufferTextureFormat
    // ==========================================
    static const std::unordered_map<std::string, FramebufferTextureFormat> kFormatMap = {
        {"R8",            FramebufferTextureFormat::R8},
        {"R32F",          FramebufferTextureFormat::R32F},
        {"RGBA8",         FramebufferTextureFormat::RGBA8},
        {"RG16F",         FramebufferTextureFormat::RG16F},
        {"RGBA16F",       FramebufferTextureFormat::RGBA16F},
        {"RGBA32F",       FramebufferTextureFormat::RGBA32F},
        {"RED_INTEGER",   FramebufferTextureFormat::RED_INTEGER},
        {"Depth",         FramebufferTextureFormat::Depth},
        {"DEPTH24STENCIL8", FramebufferTextureFormat::DEPTH24STENCIL8},
    };

    FramebufferTextureFormat PipelineBuilder::ParseFormat(const std::string& name) {
        auto it = kFormatMap.find(name);
        if (it != kFormatMap.end())
            return it->second;
        AYAYA_CORE_WARN("[PipelineBuilder] Unknown texture format '{}', falling back to RGBA8", name);
        return FramebufferTextureFormat::RGBA8;
    }

    // ==========================================
    // Constructor
    // ==========================================
    PipelineBuilder::PipelineBuilder(RenderGraph& graph)
        : m_Graph(graph) {}

    // ==========================================
    // Viewport
    // ==========================================
    void PipelineBuilder::SetViewportSize(uint32_t w, uint32_t h) {
        m_ViewportWidth = w;
        m_ViewportHeight = h;
    }
    uint32_t PipelineBuilder::GetViewportWidth() const { return m_ViewportWidth; }
    uint32_t PipelineBuilder::GetViewportHeight() const { return m_ViewportHeight; }

    // ==========================================
    // DeclareTexture
    // ==========================================
    void PipelineBuilder::DeclareTexture(const std::string& name, sol::table formatList,
                                         uint32_t width, uint32_t height,
                                         uint32_t samples, bool isShadowMap) {

        uint32_t w = (width == 0) ? m_ViewportWidth : width;
        uint32_t h = (height == 0) ? m_ViewportHeight : height;

        FramebufferSpecification spec;
        spec.Width = w;
        spec.Height = h;
        spec.Samples = samples;
        spec.IsShadowMap = isShadowMap;

        // Parse format list from Lua table
        size_t len = formatList.size();
        for (size_t i = 1; i <= len; ++i) {
            sol::object entry = formatList[i];
            if (entry.is<std::string>()) {
                FramebufferTextureSpecification texSpec;
                texSpec.TextureFormat = ParseFormat(entry.as<std::string>());
                spec.Attachments.Attachments.push_back(texSpec);
            }
        }

        if (spec.Attachments.Attachments.empty()) {
            AYAYA_CORE_WARN("[PipelineBuilder] DeclareTexture '{}' with no format attachments — skipping", name);
            return;
        }

        // Register with the RenderGraph (first-writer-wins policy)
        m_Graph.RegisterTexture(name, spec);
        m_TextureSpecs[name] = spec;
    }

    // ==========================================
    // RegisterPassInstance — per-renderer pass instances
    // ==========================================
    void PipelineBuilder::RegisterPassInstance(const std::string& name,
                                               std::shared_ptr<RenderPass> pass,
                                               std::shared_ptr<class VulkanWBOITPass> wboitPass) {
        m_PassInstances[name] = pass;
        if (wboitPass) m_WBOITPass = wboitPass;
    }

    // ==========================================
    // AddPass — the critical method
    // ==========================================
    void PipelineBuilder::AddPass(const std::string& nodeName, const std::string& passType,
                                  sol::table reads, sol::table writes, sol::table readWrites,
                                  sol::object params,
                                  uint32_t widthOverride, uint32_t heightOverride) {

        // 🔥 Validation: duplicate pass name detection
        if (m_PassNames.count(nodeName)) {
            AYAYA_CORE_ERROR("[SRP] Duplicate pass name '{}' — skipping second declaration", nodeName);
            return;
        }
        m_PassNames.insert(nodeName);

        // 1. Look up pass instance from this renderer's own instances (not global registry)
        auto it = m_PassInstances.find(passType);
        if (it == m_PassInstances.end()) {
            AYAYA_CORE_ERROR("[PipelineBuilder] Unknown pass type '{}' for node '{}'", passType, nodeName);
            return;
        }
        auto passInstance = it->second;

        // 2. Build execute function from the local pass instance
        RGExecuteFn execFn;
        if (passType == "WBOIT_Gather" && m_WBOITPass) {
            auto wp = m_WBOITPass;
            execFn = [wp](RenderContext& ctx, RenderCommandBuffer& cmd) { wp->ExecuteGather(ctx, cmd); };
        } else if (passType == "WBOIT_Resolve" && m_WBOITPass) {
            auto wp = m_WBOITPass;
            execFn = [wp](RenderContext& ctx, RenderCommandBuffer& cmd) { wp->ExecuteResolve(ctx, cmd); };
        } else {
            execFn = [passInstance](RenderContext& ctx, RenderCommandBuffer& cmd) {
                passInstance->Execute(ctx, cmd);
            };
        }

        // 3. Calculate effective dimensions
        uint32_t effectiveW = (widthOverride > 0) ? widthOverride : m_ViewportWidth;
        uint32_t effectiveH = (heightOverride > 0) ? heightOverride : m_ViewportHeight;

        // 🔥 BAKE: extract Lua params into pure C++ struct (ONCE, at setup time)
        PassBakedParams baked = BakeParams(params);
        m_BakedParams[nodeName] = baked;

        // 🔥 BAKE: extract string vectors from Lua tables (ONCE)
        auto readVec  = LuaTableToStringVector(reads);
        auto writeVec = LuaTableToStringVector(writes);
        auto rwVec    = LuaTableToStringVector(readWrites);

        // 🔥 Track texture usage for unused-texture validation
        for (auto& r : readVec)  m_UsedTextures.insert(r);
        for (auto& w : writeVec) m_UsedTextures.insert(w);
        for (auto& rw : rwVec)   m_UsedTextures.insert(rw);

        // 🔥 Pre-resolve FramebufferSpecs + bake LoadOp
        //   writes → default CLEAR (override: { ClearColor = false } → LOAD)
        //   readWrites → always LOAD (accumulation implies preserving existing content)
        bool explicitClear = baked.GetInt("ClearColor", 1) != 0;

        std::vector<std::pair<std::string, FramebufferSpecification>> writeSpecs;
        for (auto& w : writeVec) {
            FramebufferSpecification spec = GetSpecForTexture(w);
            if (!explicitClear) {
                for (auto& att : spec.Attachments.Attachments)
                    att.LoadOp = AttachmentLoadOp::Load;
            }
            writeSpecs.push_back({w, spec});
        }
        std::vector<std::pair<std::string, FramebufferSpecification>> rwSpecs;
        for (auto& rw : rwVec) {
            FramebufferSpecification spec = GetSpecForTexture(rw);
            // readWrites always LOAD — accumulating onto previous pass output
            for (auto& att : spec.Attachments.Attachments)
                att.LoadOp = AttachmentLoadOp::Load;
            rwSpecs.push_back({rw, spec});
        }

        // Save nodeName as string for the lambda capture
        std::string capturedNodeName = nodeName;

        // 4. Add the pass to the RenderGraph
        m_Graph.AddPass(nodeName,
            // ── setup_lambda (called ONCE per Compile) ──
            [&, baked, readVec, writeSpecs, rwSpecs](RGBuilder& builder) {
                // Apply Enabled culling
                if (!baked.Enabled) {
                    builder.SetCulled(true);
                    return;
                }

                // Declare explicit reads
                for (auto& r : readVec)
                    builder.ReadTexture(r);

                // Declare explicit writes (LoadOp already baked into spec)
                for (auto& [name, spec] : writeSpecs)
                    builder.WriteTexture(name, spec);

                // 🔥 ReadWrite — MUST use ReadWriteTexture, NEVER split into Read + Write!
                for (auto& [name, spec] : rwSpecs)
                    builder.ReadWriteTexture(name, spec);

                // Note: factory->DeclareResources is intentionally NOT called here.
                // The Lua-level reads/writes/readWrites above already fully declare the pass's
                // resource I/O. Calling the factory would duplicate every declaration, causing
                // InsertTileResolveBarrier to run twice per texture → VUID-01197 layout errors.
            },
            // ── execute_lambda (called EVERY FRAME) ──
            // 🔥 Captures ONLY pure C++ data!
            // ZERO sol::table. ZERO lua_State. ZERO string hashing.
            [execFn, baked, capturedNodeName](RenderContext& ctx, RenderCommandBuffer& cmd) {

                // Inject baked params into context.Settings (namespaced by node name)
                for (auto& [k, v] : baked.FloatParams)
                    ctx.Set(capturedNodeName + "." + k, v);
                for (auto& [k, v] : baked.IntParams)
                    ctx.Set(capturedNodeName + "." + k, v);
                for (auto& [k, v] : baked.StrParams)
                    ctx.Set(capturedNodeName + "." + k, v);

                // Inject LightMode and Queue tags
                if (!baked.LightMode.empty())
                    ctx.Set(capturedNodeName + ".LightMode", baked.LightMode);
                if (!baked.Queue.empty())
                    ctx.Set(capturedNodeName + ".Queue", baked.Queue);

                // Execute the pass (factory already resolved WBOIT vs standard)
                execFn(ctx, cmd);
            }
        );
    }

    // ==========================================
    // SetOutput
    // ==========================================
    void PipelineBuilder::SetOutput(const std::string& textureName) {
        m_Output = textureName;
    }

    // ==========================================
    // ValidateTextures — warn about unused declared textures
    // ==========================================
    void PipelineBuilder::ValidateTextures() {
        for (auto& [name, spec] : m_TextureSpecs) {
            if (!m_UsedTextures.count(name)) {
                AYAYA_CORE_WARN("[SRP] Texture '{}' declared but never referenced by any pass", name);
            }
        }
    }

    // ==========================================
    // Compile
    // ==========================================
    void PipelineBuilder::Compile() {
        m_Graph.Compile();
    }

    // ==========================================
    // GetBakedParams
    // ==========================================
    const PassBakedParams* PipelineBuilder::GetBakedParams(const std::string& nodeName) const {
        auto it = m_BakedParams.find(nodeName);
        return (it != m_BakedParams.end()) ? &it->second : nullptr;
    }

    // ==========================================
    // GetSpecForTexture
    // ==========================================
    FramebufferSpecification PipelineBuilder::GetSpecForTexture(const std::string& name) const {
        auto it = m_TextureSpecs.find(name);
        if (it != m_TextureSpecs.end())
            return it->second;

        AYAYA_CORE_WARN("[PipelineBuilder] Texture '{}' not declared — using default RGBA8 viewport spec", name);
        FramebufferSpecification spec;
        spec.Width = m_ViewportWidth;
        spec.Height = m_ViewportHeight;
        spec.Attachments.Attachments.push_back({FramebufferTextureFormat::RGBA8});
        return spec;
    }

    // ==========================================
    // LuaTableToStringVector — bake Lua array → vector<string>
    // ==========================================
    std::vector<std::string> PipelineBuilder::LuaTableToStringVector(sol::table t) {
        std::vector<std::string> result;
        if (!t.valid()) return result;

        size_t len = t.size();
        result.reserve(len);
        for (size_t i = 1; i <= len; ++i) {
            sol::object entry = t[i];
            if (entry.is<std::string>())
                result.push_back(entry.as<std::string>());
        }
        return result;
    }

    // ==========================================
    // BakeParams — extract sol::table → PassBakedParams (ONCE, at setup time)
    //
    // After this call, all Lua data is in pure C++ structs.
    // The hot path (execute_lambda) NEVER touches sol::table.
    // ==========================================
    PassBakedParams PipelineBuilder::BakeParams(sol::object params) {
        PassBakedParams baked;

        if (!params.is<sol::table>())
            return baked;

        sol::table pt = params.as<sol::table>();
        for (auto& kv : pt) {
            sol::object key = kv.first;
            sol::object val = kv.second;

            if (!key.is<std::string>()) continue;

            std::string keyStr = key.as<std::string>();

            // ── Special keys ──
            if (keyStr == "Enabled" && val.is<bool>()) {
                baked.Enabled = val.as<bool>();
                continue;
            }
            if (keyStr == "LightMode" && val.is<std::string>()) {
                baked.LightMode = val.as<std::string>();
                continue;
            }
            if (keyStr == "Queue" && val.is<std::string>()) {
                baked.Queue = val.as<std::string>();
                continue;
            }

            // ── Typed params ──
            if (val.is<float>()) {
                baked.FloatParams[keyStr] = val.as<float>();
            } else if (val.is<int>()) {
                // sol2 may return int as float or vice versa — check bool first since bool IS int in Lua
                sol::type vt = val.get_type();
                if (vt == sol::type::boolean) {
                    baked.IntParams[keyStr] = val.as<bool>() ? 1 : 0;
                } else {
                    baked.IntParams[keyStr] = val.as<int>();
                }
            } else if (val.is<bool>()) {
                baked.IntParams[keyStr] = val.as<bool>() ? 1 : 0;
            } else if (val.is<std::string>()) {
                baked.StrParams[keyStr] = val.as<std::string>();
            }
            // Unknown types are silently ignored
        }

        return baked;
    }

} // namespace Ayaya
