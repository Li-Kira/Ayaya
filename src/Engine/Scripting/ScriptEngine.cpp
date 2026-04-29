#include "ayapch.h"
#include "ScriptEngine.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Core/Input.hpp"
#include "Asset/AssetManager.hpp"

// ==========================================
// 引入 Box2D，供 Lua API 施加物理力使用
// ==========================================
#include <box2d/b2_body.h>

// 引入 Sol2 和 Lua
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace Ayaya {

    // 全局唯一的 Lua 状态机
    struct ScriptEngineData {
        sol::state LuaState;
    };
    static ScriptEngineData* s_Data = nullptr;

    void ScriptEngine::Init() {
        s_Data = new ScriptEngineData();
        
        // 开启 Lua 的基础库 (math, table, string 等)
        s_Data->LuaState.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);

        RegisterComponents();
        RegisterAPI();
        
        AYAYA_CORE_INFO("Lua ScriptEngine Initialized.");
    }

    void ScriptEngine::Shutdown() {
        delete s_Data;
    }

    void ScriptEngine::RegisterComponents() {
        // 1. 绑定 glm::vec2
        s_Data->LuaState.new_usertype<glm::vec2>("vec2",
            sol::constructors<glm::vec2(float, float)>(),
            "x", &glm::vec2::x,
            "y", &glm::vec2::y
        );

        // 2. 绑定 glm::vec3
        s_Data->LuaState.new_usertype<glm::vec3>("vec3",
            sol::constructors<glm::vec3(float, float, float)>(),
            "x", &glm::vec3::x,
            "y", &glm::vec3::y,
            "z", &glm::vec3::z
        );

        // 3. 绑定 TransformComponent
        s_Data->LuaState.new_usertype<TransformComponent>("TransformComponent",
            "Translation", &TransformComponent::Translation,
            "Rotation", &TransformComponent::Rotation,
            "Scale", &TransformComponent::Scale
        );

        // 4. 绑定 Rigidbody2DComponent
        s_Data->LuaState.new_usertype<Rigidbody2DComponent>("Rigidbody2DComponent",
            "ApplyLinearImpulse", [](Rigidbody2DComponent& rb, glm::vec2 impulse, bool wake) {
                b2Body* body = (b2Body*)rb.RuntimeBody;
                if (body) body->ApplyLinearImpulseToCenter(b2Vec2(impulse.x, impulse.y), wake);
            },
            "GetLinearVelocity", [](Rigidbody2DComponent& rb) -> glm::vec2 {
                b2Body* body = (b2Body*)rb.RuntimeBody;
                if (body) {
                    b2Vec2 vel = body->GetLinearVelocity();
                    return glm::vec2(vel.x, vel.y);
                }
                return glm::vec2(0.0f, 0.0f);
            },
            "SetLinearVelocity", [](Rigidbody2DComponent& rb, glm::vec2 velocity) {
                b2Body* body = (b2Body*)rb.RuntimeBody;
                if (body) body->SetLinearVelocity(b2Vec2(velocity.x, velocity.y));
            }
        );

        // 5. 绑定 Entity 类
        s_Data->LuaState.new_usertype<Entity>("Entity",
            "GetTransform", [](Entity& e) -> TransformComponent& { return e.GetComponent<TransformComponent>(); },
            "HasRigidbody2D", [](Entity& e) { return e.HasComponent<Rigidbody2DComponent>(); },
            "GetRigidbody2D", [](Entity& e) -> Rigidbody2DComponent& { return e.GetComponent<Rigidbody2DComponent>(); }
        );
    }

    void ScriptEngine::RegisterAPI() {
        // 引擎专属日志系统
        auto log = s_Data->LuaState["Log"].get_or_create<sol::table>();
        log.set_function("Info", [](const std::string& msg) { AYAYA_INFO("Lua: {0}", msg); });
        log.set_function("Warn", [](const std::string& msg) { AYAYA_WARN("Lua: {0}", msg); });
        log.set_function("Error", [](const std::string& msg) { AYAYA_ERROR("Lua: {0}", msg); });

        // Input 系统
        auto input = s_Data->LuaState["Input"].get_or_create<sol::table>();
        input.set_function("IsKeyPressed", &Input::IsKeyPressed);
        
        auto key = s_Data->LuaState["Key"].get_or_create<sol::table>();
        key["Space"] = Key::Space; 
        key["W"] = Key::W;
        key["A"] = Key::A;
        key["S"] = Key::S;
        key["D"] = Key::D;
        key["Up"] = Key::Up;
        key["Down"] = Key::Down;
        key["Left"] = Key::Left;
        key["Right"] = Key::Right;
    }

    void ScriptEngine::OnCreateEntity(Entity entity) {
        auto& lsc = entity.GetComponent<LuaScriptComponent>();
        
        // 【核心修复 1】：检查 UUID 是否合法
        if (lsc.ScriptHandle == 0) return;

        // 【核心修复 2】：向 AssetManager 询问这个 Handle 对应的电脑物理绝对路径
        std::string physicalPath = AssetManager::GetAssetPhysicalPath(lsc.ScriptHandle);
        if (physicalPath.empty()) {
            AYAYA_CORE_ERROR("Lua Error: Script asset is missing or invalid for Handle {0}", (uint64_t)lsc.ScriptHandle);
            return;
        }

        // 为这个实体创建一个专属的 Lua Environment (沙盒)
        sol::environment& env = *(new sol::environment(s_Data->LuaState, sol::create, s_Data->LuaState.globals()));
        lsc.RuntimeEnvironment = &env;

        // 将 C++ 的 Entity 对象直接塞进这个沙盒中
        env["entity"] = entity;

        try {
            // 【核心修复 3】：使用查到的真实物理路径来让底层 Lua 读取文本文件
            s_Data->LuaState.script_file(physicalPath, env);
            
            sol::protected_function onCreateFunc = env["OnCreate"];
            if (onCreateFunc.valid()) {
                onCreateFunc();
            }
        } catch (const sol::error& err) {
            // 报错信息也显示真实路径，方便你排查
            AYAYA_CORE_ERROR("Lua Error in {0}: {1}", physicalPath, err.what());
        }
    }

    void ScriptEngine::OnUpdateEntity(Entity entity, Timestep ts) {
        auto& lsc = entity.GetComponent<LuaScriptComponent>();
        if (!lsc.RuntimeEnvironment) return;

        sol::environment& env = *(sol::environment*)lsc.RuntimeEnvironment;

        sol::protected_function onUpdateFunc = env["OnUpdate"];
        if (onUpdateFunc.valid()) {
            auto result = onUpdateFunc(ts.GetSeconds());
            if (!result.valid()) {
                sol::error err = result;
                AYAYA_CORE_ERROR("Lua Update Error: {0}", err.what());
            }
        }
    }
}