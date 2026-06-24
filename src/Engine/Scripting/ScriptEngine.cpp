#include "ayapch.h"
#include "ScriptEngine.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Core/Input.hpp"
#include "Asset/AssetManager.hpp"
#include "Asset/Prefab.hpp"
#include "Renderer/PipelineBuilder.hpp"

#include <chrono>
#include <filesystem>
#include <box2d/b2_body.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace Ayaya {

    struct ScriptEngineData {
        sol::state LuaState;
    };
    static ScriptEngineData* s_Data = nullptr;

    // Per-entity CONFIG metadata cache (UUID → params)
    static std::unordered_map<UUID, std::vector<ScriptParamMeta>> s_ParamCache;

    // ---- internal helper: get the sol::environment for an entity ----
    static sol::environment* GetEnv(Entity entity) {
        auto& lsc = entity.GetComponent<LuaScriptComponent>();
        return (sol::environment*)lsc.RuntimeEnvironment;
    }

    // ---- load script + inject entity/scene, optionally call OnCreate ----
    static void LoadScriptIntoEnv(Entity entity, Scene* scene, bool callOnCreate) {
        auto& lsc = entity.GetComponent<LuaScriptComponent>();
        if (lsc.ScriptHandle == 0) return;

        std::string physicalPath = AssetManager::GetAssetPhysicalPath(lsc.ScriptHandle);
        if (physicalPath.empty()) {
            AYAYA_CORE_ERROR("Lua: script asset missing for handle {0}", (uint64_t)lsc.ScriptHandle);
            return;
        }

        // Already initialized?  Check for hot-reload (file modified on disk).
        if (lsc.RuntimeEnvironment) {
            try {
                auto ftime = std::filesystem::last_write_time(physicalPath);
                uint64_t mtime = std::chrono::duration_cast<std::chrono::seconds>(
                    ftime.time_since_epoch()).count();
                if (mtime != lsc.ScriptFileMtime && lsc.ScriptFileMtime != 0) {
                    AYAYA_CORE_INFO("Lua hot-reload: {0}", physicalPath);
                    ScriptEngine::ReleaseScriptEnv(entity);
                    // Fall through to reload below
                } else {
                    lsc.ScriptFileMtime = mtime;
                    return;  // unchanged
                }
            } catch (...) {
                return;  // can't stat file — keep existing env
            }
        }

        sol::environment& env = *(new sol::environment(s_Data->LuaState, sol::create, s_Data->LuaState.globals()));
        lsc.RuntimeEnvironment = &env;
        env["entity"] = entity;
        if (scene) env["scene"] = scene;

        try {
            s_Data->LuaState.script_file(physicalPath, env);

            // Record mtime for future hot-reload detection
            try {
                auto ftime = std::filesystem::last_write_time(physicalPath);
                lsc.ScriptFileMtime = std::chrono::duration_cast<std::chrono::seconds>(
                    ftime.time_since_epoch()).count();
            } catch (...) {}

            if (callOnCreate) {
                sol::protected_function fn = env["OnCreate"];
                if (fn.valid()) fn();
            }
        } catch (const sol::error& err) {
            AYAYA_CORE_ERROR("Lua Error in {0}: {1}", physicalPath, err.what());
        }

        // Parse CONFIG table declared by the script
        ScriptEngine::ParseConfig(entity);
    }

    // ---- parse the Lua CONFIG table into s_ParamCache ----
    void ScriptEngine::ParseConfig(Entity entity) {
        auto* env = GetEnv(entity);
        if (!env) return;

        auto& lsc = entity.GetComponent<LuaScriptComponent>();
        UUID entityUUID = entity.GetComponent<IDComponent>().ID;
        auto& meta = s_ParamCache[entityUUID];
        meta.clear();

        sol::object cfg = (*env)["CONFIG"];
        if (!cfg.is<sol::table>()) return;

        sol::table config = cfg;
        for (auto& kv : config) {
            sol::table entry = kv.second.as<sol::table>();
            if (!entry.valid()) continue;

            ScriptParamMeta m;
            m.Name  = kv.first.as<std::string>();
            m.Label = entry["label"].get_or(m.Name);
            m.Type  = entry["type"].get_or(std::string("int"));

            if (m.Type == "int" || m.Type == "float") {
                m.Min = entry["min"].get_or(0.0f);
                m.Max = entry["max"].get_or(100.0f);
            }
            if (m.Type == "file") {
                m.FileExt = entry["ext"].get_or(std::string(""));
            }
            if (m.Type == "combo") {
                sol::table opts = entry["options"];
                if (opts.valid()) {
                    for (size_t i = 1; i <= opts.size(); ++i)
                        m.ComboOpts.push_back(opts[i].get_or(std::string("?")));
                }
            }

            // Seed Lua global: persisted override > CONFIG default
            auto it = lsc.ConfigOverrides.find(m.Name);
            if (it != lsc.ConfigOverrides.end()) {
                // Restore saved value based on type
                try {
                    if (m.Type == "int")        (*env)[m.Name] = std::stoi(it->second);
                    else if (m.Type == "float")  (*env)[m.Name] = std::stof(it->second);
                    else if (m.Type == "file" || m.Type == "combo")
                        (*env)[m.Name] = it->second;  // string
                } catch (...) {
                    // Corrupt override — fall through to default
                    sol::object def = entry["value"];
                    if (def.valid()) (*env)[m.Name] = def;
                }
            } else {
                sol::object def = entry["value"];
                if (def.valid()) {
                    sol::object cur = (*env)[m.Name];
                    if (cur == sol::lua_nil) (*env)[m.Name] = def;
                }
            }

            meta.push_back(std::move(m));
        }
    }

    // ---- Per-frame dirty tracking (avoids N rebuilds during drag) ----
    static std::unordered_set<UUID> s_DirtySpawners;
    static void MarkDirty(Entity entity) {
        if (entity.HasComponent<IDComponent>())
            s_DirtySpawners.insert(entity.GetComponent<IDComponent>().ID);
    }

    // ========================================================================
    // Public API
    // ========================================================================

    void ScriptEngine::Init() {
        s_Data = new ScriptEngineData();
        s_Data->LuaState.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
        RegisterComponents();
        RegisterAPI();
        AYAYA_CORE_INFO("Lua ScriptEngine Initialized.");
    }

    void ScriptEngine::Shutdown() {
        delete s_Data;
        s_Data = nullptr;
    }

    sol::state& ScriptEngine::GetLuaState() {
        return s_Data->LuaState;
    }

    // ---- Runtime ----

    void ScriptEngine::OnCreateEntity(Entity entity, Scene* scene) {
        ReleaseScriptEnv(entity);  // discard stale editor env, reload fresh
        LoadScriptIntoEnv(entity, scene, true);
    }

    void ScriptEngine::OnUpdateEntity(Entity entity, Timestep ts) {
        auto* env = GetEnv(entity);
        if (!env) return;

        sol::protected_function fn = (*env)["OnUpdate"];
        if (fn.valid()) {
            auto result = fn(ts.GetSeconds());
            if (!result.valid()) {
                sol::error err = result;
                AYAYA_CORE_ERROR("Lua Update Error: {0}", err.what());
            }
        }
    }

    // ---- Editor mode ----

    void ScriptEngine::InitEditorScript(Entity entity, Scene* scene) {
        LoadScriptIntoEnv(entity, scene, false);
    }

    void ScriptEngine::OnEditorUpdate(Scene* scene, Timestep ts) {
        // Rebuild dirty spawners at most once per frame (CONFIG widgets may have
        // fired SetConfig* dozens of times during a drag).
        std::unordered_set<UUID> toProcess;
        toProcess.swap(s_DirtySpawners);

        scene->Reg().view<LuaScriptComponent>().each([&](auto entityID, auto& lsc) {
            Entity entity{ entityID, scene };
            auto* env = GetEnv(entity);
            bool justInitialized = (env == nullptr);
            if (!env) {
                InitEditorScript(entity, scene);
                env = GetEnv(entity);
                justInitialized = true;
            }
            if (!env) return;

            // First frame after project load / script assign → clean slate + rebuild
            if (justInitialized) {
                if (entity.HasComponent<RelationshipComponent>()) {
                    auto& rel = entity.GetComponent<RelationshipComponent>();
                    auto children = rel.Children;
                    for (auto childID : children) {
                        if (scene->Reg().valid(childID))
                            scene->DestroyEntity(Entity{childID, scene});
                    }
                    rel.Children.clear();
                }
                TriggerRebuild(entity, scene);
            }
            // Per-frame rebuild: only if a CONFIG widget changed this frame
            else if (entity.HasComponent<IDComponent>()) {
                if (toProcess.count(entity.GetComponent<IDComponent>().ID)) {
                    TriggerRebuild(entity, scene);
                }
            }

            sol::protected_function fn = (*env)["OnEditorUpdate"];
            if (fn.valid()) {
                auto result = fn(ts.GetSeconds());
                if (!result.valid()) {
                    sol::error err = result;
                    AYAYA_CORE_ERROR("Lua Editor Error: {0}", err.what());
                }
            }
        });
    }

    // ---- CONFIG accessors ----

    const std::vector<ScriptParamMeta>& ScriptEngine::GetScriptParams(Entity entity) {
        static std::vector<ScriptParamMeta> empty;
        UUID id = entity.GetComponent<IDComponent>().ID;
        auto it = s_ParamCache.find(id);
        return it != s_ParamCache.end() ? it->second : empty;
    }

    int ScriptEngine::GetConfigInt(Entity entity, const std::string& name, int def) {
        auto* env = GetEnv(entity);
        if (!env) return def;
        return (*env)[name].get_or(def);
    }

    float ScriptEngine::GetConfigFloat(Entity entity, const std::string& name, float def) {
        auto* env = GetEnv(entity);
        if (!env) return def;
        return (*env)[name].get_or(def);
    }

    uint64_t ScriptEngine::GetConfigU64(Entity entity, const std::string& name, uint64_t def) {
        auto* env = GetEnv(entity);
        if (!env) return def;
        return (*env)[name].get_or(def);
    }

    std::string ScriptEngine::GetConfigStr(Entity entity, const std::string& name, const std::string& def) {
        auto* env = GetEnv(entity);
        if (!env) return def;
        return (*env)[name].get_or(def);
    }

    void ScriptEngine::SetConfigInt(Entity entity, const std::string& name, int v) {
        auto* env = GetEnv(entity);
        if (env) {
            (*env)[name] = v;
            entity.GetComponent<LuaScriptComponent>().ConfigOverrides[name] = std::to_string(v);
            MarkDirty(entity);
        }
    }

    void ScriptEngine::SetConfigFloat(Entity entity, const std::string& name, float v) {
        auto* env = GetEnv(entity);
        if (env) {
            (*env)[name] = v;
            entity.GetComponent<LuaScriptComponent>().ConfigOverrides[name] = std::to_string(v);
            MarkDirty(entity);
        }
    }

    void ScriptEngine::SetConfigU64(Entity entity, const std::string& name, uint64_t v) {
        auto* env = GetEnv(entity);
        if (env) {
            (*env)[name] = v;
            entity.GetComponent<LuaScriptComponent>().ConfigOverrides[name] = std::to_string(v);
            MarkDirty(entity);
        }
    }

    void ScriptEngine::SetConfigStr(Entity entity, const std::string& name, const std::string& v) {
        auto* env = GetEnv(entity);
        if (env) {
            (*env)[name] = v;
            entity.GetComponent<LuaScriptComponent>().ConfigOverrides[name] = v;
            MarkDirty(entity);
        }
    }

    void ScriptEngine::ReleaseScriptEnv(Entity entity) {
        auto& lsc = entity.GetComponent<LuaScriptComponent>();
        if (lsc.RuntimeEnvironment) {
            delete (sol::environment*)lsc.RuntimeEnvironment;
            lsc.RuntimeEnvironment = nullptr;
        }
    }

    void ScriptEngine::TriggerRebuild(Entity entity, Scene* scene) {
        if (!entity || !scene) return;
        auto* env = GetEnv(entity);
        if (!env) return;

        // Purge stale entries from the Lua spawned table.
        // Use sol::object to safely handle nil / non-table values.
        sol::object spawnedObj = (*env)["spawned"];
        if (spawnedObj.is<sol::table>()) {
            sol::table spawned = spawnedObj;
            int n = (int)spawned.size();
            for (int i = n; i >= 1; --i) {
                sol::object obj = spawned[i];
                if (obj.is<Entity>()) {
                    Entity e = obj.as<Entity>();
                    if (!e || !scene->Reg().valid(e.GetEntityHandle())) {
                        spawned[i] = sol::lua_nil;
                    }
                }
            }
        }

        (*env)["scene"] = scene;  // refresh scene ref
        sol::protected_function fn = (*env)["Rebuild"];
        if (fn.valid()) {
            auto result = fn(entity, scene);
            if (!result.valid()) {
                sol::error err = result;
                AYAYA_CORE_ERROR("Lua Rebuild Error: {0}", err.what());
            }
        }
    }

    // ========================================================================
    // Component & API registration
    // ========================================================================

    void ScriptEngine::RegisterComponents() {
        // 1. glm::vec2
        s_Data->LuaState.new_usertype<glm::vec2>("vec2",
            sol::constructors<glm::vec2(float, float)>(),
            "x", &glm::vec2::x, "y", &glm::vec2::y);

        // 2. glm::vec3
        s_Data->LuaState.new_usertype<glm::vec3>("vec3",
            sol::constructors<glm::vec3(float, float, float)>(),
            "x", &glm::vec3::x, "y", &glm::vec3::y, "z", &glm::vec3::z);

        // 3. TransformComponent
        s_Data->LuaState.new_usertype<TransformComponent>("TransformComponent",
            "Translation", &TransformComponent::Translation,
            "Rotation",    &TransformComponent::Rotation,
            "Scale",       &TransformComponent::Scale);

        // 4. Rigidbody2DComponent
        s_Data->LuaState.new_usertype<Rigidbody2DComponent>("Rigidbody2DComponent",
            "ApplyLinearImpulse", [](Rigidbody2DComponent& rb, glm::vec2 impulse, bool wake) {
                b2Body* body = (b2Body*)rb.RuntimeBody;
                if (body) body->ApplyLinearImpulseToCenter(b2Vec2(impulse.x, impulse.y), wake);
            },
            "GetLinearVelocity", [](Rigidbody2DComponent& rb) -> glm::vec2 {
                b2Body* body = (b2Body*)rb.RuntimeBody;
                if (body) { b2Vec2 vel = body->GetLinearVelocity(); return glm::vec2(vel.x, vel.y); }
                return glm::vec2(0);
            },
            "SetLinearVelocity", [](Rigidbody2DComponent& rb, glm::vec2 v) {
                b2Body* body = (b2Body*)rb.RuntimeBody;
                if (body) body->SetLinearVelocity(b2Vec2(v.x, v.y));
            });

        // 5. Entity
        s_Data->LuaState.new_usertype<Entity>("Entity",
            "GetTransform",    [](Entity& e) -> TransformComponent& { return e.GetComponent<TransformComponent>(); },
            "HasRigidbody2D",  [](Entity& e) { return e.HasComponent<Rigidbody2DComponent>(); },
            "GetRigidbody2D",  [](Entity& e) -> Rigidbody2DComponent& { return e.GetComponent<Rigidbody2DComponent>(); },
            "SetTranslation",  [](Entity& e, glm::vec3 v) { e.GetComponent<TransformComponent>().Translation = v; },
            "SetRotation",     [](Entity& e, glm::vec3 v) { e.GetComponent<TransformComponent>().Rotation = v; },
            "SetScale",        [](Entity& e, glm::vec3 v) { e.GetComponent<TransformComponent>().Scale = v; },
            "GetTag",          [](Entity& e) -> std::string { return e.GetComponent<TagComponent>().Tag; },
            "SetTag",          [](Entity& e, const std::string& t) { e.GetComponent<TagComponent>().Tag = t; },
            "IsValid",         [](Entity& e) -> bool { return (bool)e; },
            "SetParent",       [](Entity& e, Entity parent) { e.SetParent(parent, false); },
            "Destroy",         [](Entity& e) { Scene* s = e.GetScene(); if (s) s->DestroyEntity(e); }
        );

        // 6. Scene
        s_Data->LuaState.new_usertype<Scene>("Scene",
            "CreateEntity",      [](Scene& s, const std::string& name) -> Entity { return s.CreateEntity(name); },
            "DestroyEntity",     [](Scene& s, Entity e) { s.DestroyEntity(e); },
            "InstantiatePrefab", [](Scene& s, sol::object handle) -> Entity {
                uint64_t h = 0;
                if (handle.is<std::string>()) {
                    try {
                        std::string str = handle.as<std::string>();
                        if (str.empty()) return Entity{};
                        h = std::stoull(str);
                    } catch (const std::exception& e) {
                        AYAYA_CORE_ERROR("Lua InstantiatePrefab: invalid UUID string — {0}", e.what());
                        return Entity{};
                    } catch (...) {
                        AYAYA_CORE_ERROR("Lua InstantiatePrefab: unknown error parsing UUID");
                        return Entity{};
                    }
                } else if (handle.is<uint64_t>()) {
                    h = handle.as<uint64_t>();
                } else if (handle.is<int64_t>() && handle.as<int64_t>() > 0) {
                    h = (uint64_t)handle.as<int64_t>();
                }
                if (h == 0) return Entity{};
                auto prefab = AssetManager::GetAsset<Prefab>(UUID(h));
                if (!prefab) {
                    AYAYA_CORE_WARN("Lua InstantiatePrefab: prefab asset not found for UUID {0}", h);
                    return Entity{};
                }
                return s.InstantiatePrefab(prefab.get());
            }
        );
    }

    void ScriptEngine::RegisterAPI() {
        auto log = s_Data->LuaState["Log"].get_or_create<sol::table>();
        log.set_function("Info",  [](const std::string& msg) { AYAYA_INFO("Lua: {0}", msg); });
        log.set_function("Warn",  [](const std::string& msg) { AYAYA_WARN("Lua: {0}", msg); });
        log.set_function("Error", [](const std::string& msg) { AYAYA_ERROR("Lua: {0}", msg); });

        auto input = s_Data->LuaState["Input"].get_or_create<sol::table>();
        input.set_function("IsKeyPressed", &Input::IsKeyPressed);

        auto key = s_Data->LuaState["Key"].get_or_create<sol::table>();
        key["Space"] = Key::Space;
        key["W"] = Key::W;  key["A"] = Key::A;  key["S"] = Key::S;  key["D"] = Key::D;
        key["R"] = Key::R;
        key["Up"] = Key::Up;    key["Down"]  = Key::Down;
        key["Left"] = Key::Left; key["Right"] = Key::Right;
        key["_1"] = Key::D1;  key["_2"] = Key::D2;
        key["_3"] = Key::D3;  key["_4"] = Key::D4;

        // Batch API: flat float arrays for minimum Lua→C++ overhead
        auto ayaya = s_Data->LuaState["Ayaya"].get_or_create<sol::table>();
        ayaya.set_function("SetTranslationsBatch", [](sol::table entities, std::vector<float> flat) {
            size_t n = std::min(entities.size(), flat.size() / 3);
            for (size_t i = 0; i < n; ++i) {
                sol::object eObj = entities[i + 1];
                if (eObj.is<Entity>()) {
                    Entity e = eObj.as<Entity>();
                    if (e) {
                        e.GetComponent<TransformComponent>().Translation =
                            glm::vec3(flat[i*3], flat[i*3+1], flat[i*3+2]);
                    }
                }
            }
        });
        ayaya.set_function("SetRotationsBatch", [](sol::table entities, std::vector<float> flat) {
            size_t n = std::min(entities.size(), flat.size() / 3);
            for (size_t i = 0; i < n; ++i) {
                sol::object eObj = entities[i + 1];
                if (eObj.is<Entity>()) {
                    Entity e = eObj.as<Entity>();
                    if (e) {
                        e.GetComponent<TransformComponent>().Rotation =
                            glm::vec3(flat[i*3], flat[i*3+1], flat[i*3+2]);
                    }
                }
            }
        });

        // ── SRP: PipelineBuilder (Scriptable Render Pipeline) ──
        // Note: sol2 does NOT forward C++ default arg values to Lua.
        // We use sol::overload with explicit arity overloads for methods with defaults.
        s_Data->LuaState.new_usertype<PipelineBuilder>("PipelineBuilder",
            "SetOutput",         &PipelineBuilder::SetOutput,
            "GetViewportWidth",  &PipelineBuilder::GetViewportWidth,
            "GetViewportHeight", &PipelineBuilder::GetViewportHeight,

            // DeclareTexture — 4/5/6 arg overloads
            // Width/height/samples accept Lua number (int or float), cast to uint32_t internally.
            "DeclareTexture", sol::overload(
                [](PipelineBuilder& self, const std::string& name, sol::table formats,
                   double w, double h) {
                    self.DeclareTexture(name, formats, (uint32_t)w, (uint32_t)h);
                },
                [](PipelineBuilder& self, const std::string& name, sol::table formats,
                   double w, double h, double samples) {
                    self.DeclareTexture(name, formats, (uint32_t)w, (uint32_t)h, (uint32_t)samples);
                },
                [](PipelineBuilder& self, const std::string& name, sol::table formats,
                   double w, double h, double samples, bool isShadowMap) {
                    self.DeclareTexture(name, formats, (uint32_t)w, (uint32_t)h, (uint32_t)samples, isShadowMap);
                }
            ),

            // AddPass — 5/6/7/8 arg overloads
            // params defaults to nil, widthOverride/heightOverride default to 0.
            "AddPass", sol::overload(
                // 5 args: no params
                [](PipelineBuilder& self, const std::string& nodeName,
                   const std::string& passType, sol::table reads, sol::table writes,
                   sol::table readWrites) {
                    self.AddPass(nodeName, passType, reads, writes, readWrites, sol::lua_nil);
                },
                // 6 args: with params
                [](PipelineBuilder& self, const std::string& nodeName,
                   const std::string& passType, sol::table reads, sol::table writes,
                   sol::table readWrites, sol::object params) {
                    self.AddPass(nodeName, passType, reads, writes, readWrites, params);
                },
                // 7 args: params + wOverride
                [](PipelineBuilder& self, const std::string& nodeName,
                   const std::string& passType, sol::table reads, sol::table writes,
                   sol::table readWrites, sol::object params,
                   double wOverride) {
                    self.AddPass(nodeName, passType, reads, writes, readWrites, params, (uint32_t)wOverride);
                },
                // 8 args: params + wOverride + hOverride
                [](PipelineBuilder& self, const std::string& nodeName,
                   const std::string& passType, sol::table reads, sol::table writes,
                   sol::table readWrites, sol::object params,
                   double wOverride, double hOverride) {
                    self.AddPass(nodeName, passType, reads, writes, readWrites, params,
                                (uint32_t)wOverride, (uint32_t)hOverride);
                }
            )
        );
    }

}
