#pragma once
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Forward-declare sol::state to avoid pulling in <sol/sol.hpp> everywhere
namespace sol { class state; }

namespace Ayaya {

    // Metadata for one CONFIG parameter declared by a Lua script.
    // Read by PropertiesPanel to render the appropriate widget.
    struct ScriptParamMeta {
        std::string Name;          // global variable name in Lua env
        std::string Label;         // display name in the UI
        std::string Type;          // "int" | "float" | "file" | "combo" | "bool"
        float Min = 0, Max = 100;
        std::string FileExt;       // for "file" type, e.g. ".prefab"
        std::vector<std::string> ComboOpts;
    };

    class ScriptEngine {
    public:
        static void Init();
        static void Shutdown();

        // ---- Runtime (Play mode) ----
        static void OnCreateEntity(Entity entity, Scene* scene);
        static void OnUpdateEntity(Entity entity, Timestep ts);

        // ---- Editor mode ----
        // Create the Lua env / load the script / parse CONFIG (does NOT call OnCreate).
        static void InitEditorScript(Entity entity, Scene* scene);
        // Per-frame editor update — calls OnEditorUpdate(ts) if the script defines it.
        static void OnEditorUpdate(Scene* scene, Timestep ts);

        // ---- CONFIG parameters (editor UI) ----
        static const std::vector<ScriptParamMeta>& GetScriptParams(Entity entity);
        static int         GetConfigInt(Entity entity, const std::string& name, int def);
        static float       GetConfigFloat(Entity entity, const std::string& name, float def);
        static uint64_t    GetConfigU64(Entity entity, const std::string& name, uint64_t def);
        static std::string GetConfigStr(Entity entity, const std::string& name, const std::string& def);
        static void        SetConfigInt(Entity entity, const std::string& name, int v);
        static void        SetConfigFloat(Entity entity, const std::string& name, float v);
        static void        SetConfigU64(Entity entity, const std::string& name, uint64_t v);
        static void        SetConfigStr(Entity entity, const std::string& name, const std::string& v);
        // Call the script's Rebuild(spawnerEntity, scene) function.
        static void        TriggerRebuild(Entity entity, Scene* scene);
        // Release the Lua environment (call before changing/removing script handle)
        static void        ReleaseScriptEnv(Entity entity);

        // Access the raw Lua state (for pipeline scripting, etc.)
        static sol::state& GetLuaState();

    private:
        static void RegisterComponents();
        static void RegisterAPI();

    public:
        // Called internally after script load to cache CONFIG metadata
        static void ParseConfig(Entity entity);
    };

}
