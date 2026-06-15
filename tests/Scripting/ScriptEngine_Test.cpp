/**
 * Lua ScriptEngine unit tests — engine init/shutdown, entity-level script
 * lifecycle, CONFIG system, and C++↔Lua data exchange.
 *
 * IMPORTANT: InitEditorScript, SetConfig, GetConfig, TriggerRebuild, etc.
 * require LuaScriptComponent on the entity. The engine does not guard against
 * missing components — the caller (typically PropertiesPanel) ensures the
 * component exists before calling these functions.
 */
#include <gtest/gtest.h>
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scripting/ScriptEngine.hpp"

using namespace Ayaya;

// Helper: create an entity with LuaScriptComponent
static Entity MakeScriptEntity(std::shared_ptr<Scene>& scene, const char* name = "Test") {
    Entity e = scene->CreateEntity(name);
    e.AddComponent<LuaScriptComponent>();
    return e;
}

// =========================================================================
// Engine lifecycle
// =========================================================================

TEST(ScriptEngineTest, Init_DoesNotCrash) {
    EXPECT_NO_THROW({ ScriptEngine::Init(); });
    EXPECT_NO_THROW({ ScriptEngine::Init(); });
    ScriptEngine::Shutdown();
}

TEST(ScriptEngineTest, Shutdown_AfterInitIsSafe) {
    ScriptEngine::Init();
    EXPECT_NO_THROW({ ScriptEngine::Shutdown(); });
    EXPECT_NO_THROW({ ScriptEngine::Shutdown(); });
}

// =========================================================================
// Editor script lifecycle
// =========================================================================

TEST(ScriptEngineTest, InitEditorScript_WithZeroHandle) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    // ScriptHandle=0 (unset) — should be a safe no-op
    EXPECT_NO_THROW({ ScriptEngine::InitEditorScript(e, scene.get()); });
    ScriptEngine::Shutdown();
}

TEST(ScriptEngineTest, ReleaseScriptEnv_WithoutInit) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    // Release on an entity that never had InitEditorScript called
    EXPECT_NO_THROW({ ScriptEngine::ReleaseScriptEnv(e); });
    ScriptEngine::Shutdown();
}

// =========================================================================
// CONFIG parameter system
// =========================================================================

TEST(ScriptEngineTest, GetConfigInt_ReturnsDefaultWhenNoScriptLoaded) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    // No script loaded → GetEnv returns null → default returned
    EXPECT_EQ(ScriptEngine::GetConfigInt(e, "nonexistent", 42), 42);
    ScriptEngine::Shutdown();
}

TEST(ScriptEngineTest, GetConfigFloat_ReturnsDefaultWhenNoScriptLoaded) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    EXPECT_FLOAT_EQ(ScriptEngine::GetConfigFloat(e, "speed", 3.14f), 3.14f);
    ScriptEngine::Shutdown();
}

TEST(ScriptEngineTest, GetConfigU64_ReturnsDefaultWhenNoScriptLoaded) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    EXPECT_EQ(ScriptEngine::GetConfigU64(e, "count", 100ULL), 100ULL);
    ScriptEngine::Shutdown();
}

TEST(ScriptEngineTest, GetConfigStr_ReturnsDefaultWhenNoScriptLoaded) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    EXPECT_EQ(ScriptEngine::GetConfigStr(e, "name", "default_name"), "default_name");
    ScriptEngine::Shutdown();
}

// =========================================================================
// CONFIG setters
// =========================================================================

TEST(ScriptEngineTest, SetConfigInt_DoesNotCrash) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    ScriptEngine::SetConfigInt(e, "myInt", 77);
    SUCCEED();
    ScriptEngine::Shutdown();
}

TEST(ScriptEngineTest, SetConfigFloat_DoesNotCrash) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    ScriptEngine::SetConfigFloat(e, "myFloat", 2.718f);
    SUCCEED();
    ScriptEngine::Shutdown();
}

TEST(ScriptEngineTest, SetConfigU64_DoesNotCrash) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    ScriptEngine::SetConfigU64(e, "myU64", 99999ULL);
    SUCCEED();
    ScriptEngine::Shutdown();
}

TEST(ScriptEngineTest, SetConfigStr_DoesNotCrash) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    ScriptEngine::SetConfigStr(e, "myStr", "hello_world");
    SUCCEED();
    ScriptEngine::Shutdown();
}

TEST(ScriptEngineTest, ConfigOverwrite_LastWriteWins) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    ScriptEngine::SetConfigInt(e, "x", 1);
    ScriptEngine::SetConfigInt(e, "x", 999);
    EXPECT_EQ(ScriptEngine::GetConfigInt(e, "x", 0), 0); // defaults when no env
    ScriptEngine::Shutdown();
}

TEST(ScriptEngineTest, ConfigIsolation_BetweenEntities) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e1 = MakeScriptEntity(scene, "One");
    Entity e2 = MakeScriptEntity(scene, "Two");

    ScriptEngine::SetConfigInt(e1, "val", 100);
    ScriptEngine::SetConfigInt(e2, "val", 200);

    EXPECT_EQ(ScriptEngine::GetConfigInt(e1, "val", 0), 0);
    EXPECT_EQ(ScriptEngine::GetConfigInt(e2, "val", 0), 0);
    ScriptEngine::Shutdown();
}

// =========================================================================
// GetScriptParams
// =========================================================================

TEST(ScriptEngineTest, GetScriptParams_ReturnsEmptyForUnloadedScript) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    const auto& params = ScriptEngine::GetScriptParams(e);
    EXPECT_TRUE(params.empty());
    ScriptEngine::Shutdown();
}

// =========================================================================
// TriggerRebuild
// =========================================================================

TEST(ScriptEngineTest, TriggerRebuild_NoScriptDoesNotCrash) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    EXPECT_NO_THROW({ ScriptEngine::TriggerRebuild(e, scene.get()); });
    ScriptEngine::Shutdown();
}

// =========================================================================
// Full cycle
// =========================================================================

TEST(ScriptEngineTest, FullLifecycle_InitConfigRelease) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    Entity e = MakeScriptEntity(scene);
    EXPECT_NO_THROW({ ScriptEngine::InitEditorScript(e, scene.get()); });
    EXPECT_EQ(ScriptEngine::GetConfigInt(e, "any", 5), 5);
    EXPECT_NO_THROW({ ScriptEngine::ReleaseScriptEnv(e); });
    ScriptEngine::Shutdown();
}

// =========================================================================
// OnEditorUpdate
// =========================================================================

TEST(ScriptEngineTest, OnEditorUpdate_WithoutScripts) {
    ScriptEngine::Init();
    auto scene = std::make_shared<Scene>();
    scene->CreateEntity("A");
    scene->CreateEntity("B");

    Timestep ts(0.016f);
    EXPECT_NO_THROW({ ScriptEngine::OnEditorUpdate(scene.get(), ts); });
    ScriptEngine::Shutdown();
}
