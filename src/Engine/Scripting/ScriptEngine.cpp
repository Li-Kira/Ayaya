#include "ayapch.h"
#include "ScriptEngine.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Core/Input.hpp"

// ==========================================
// 新增：引入 Box2D，供 Lua API 施加物理力使用
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
        // 1. 绑定 glm::vec2 (物理系统和 2D 游戏极度依赖)
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

        // ==========================================
        // 新增 4：绑定 Rigidbody2DComponent (黑魔法封装！)
        // ==========================================
        s_Data->LuaState.new_usertype<Rigidbody2DComponent>("Rigidbody2DComponent",
            // 施加瞬间的冲量 (例如：跳跃、子弹发射)
            "ApplyLinearImpulse", [](Rigidbody2DComponent& rb, glm::vec2 impulse, bool wake) {
                b2Body* body = (b2Body*)rb.RuntimeBody;
                if (body) body->ApplyLinearImpulseToCenter(b2Vec2(impulse.x, impulse.y), wake);
            },
            // 获取当前线速度
            "GetLinearVelocity", [](Rigidbody2DComponent& rb) -> glm::vec2 {
                b2Body* body = (b2Body*)rb.RuntimeBody;
                if (body) {
                    b2Vec2 vel = body->GetLinearVelocity();
                    return glm::vec2(vel.x, vel.y);
                }
                return glm::vec2(0.0f, 0.0f);
            },
            // 强制设置速度 (例如：匀速传送带)
            "SetLinearVelocity", [](Rigidbody2DComponent& rb, glm::vec2 velocity) {
                b2Body* body = (b2Body*)rb.RuntimeBody;
                if (body) body->SetLinearVelocity(b2Vec2(velocity.x, velocity.y));
            }
        );

        // 5. 绑定 Entity 类，提供安全的组件获取接口
        s_Data->LuaState.new_usertype<Entity>("Entity",
            "GetTransform", [](Entity& e) -> TransformComponent& { return e.GetComponent<TransformComponent>(); },
            
            // 新增：安全获取物理组件
            "HasRigidbody2D", [](Entity& e) { return e.HasComponent<Rigidbody2DComponent>(); },
            "GetRigidbody2D", [](Entity& e) -> Rigidbody2DComponent& { return e.GetComponent<Rigidbody2DComponent>(); }
        );
    }

    void ScriptEngine::RegisterAPI() {
        // ==========================================
        // 1. 引擎专属日志系统
        // ==========================================
        auto log = s_Data->LuaState["Log"].get_or_create<sol::table>();
        log.set_function("Info", [](const std::string& msg) { AYAYA_INFO("Lua: {0}", msg); });
        log.set_function("Warn", [](const std::string& msg) { AYAYA_WARN("Lua: {0}", msg); });
        log.set_function("Error", [](const std::string& msg) { AYAYA_ERROR("Lua: {0}", msg); });

        // 2. 暴露 Input 系统给 Lua
        auto input = s_Data->LuaState["Input"].get_or_create<sol::table>();
        input.set_function("IsKeyPressed", &Input::IsKeyPressed);
        
        // ==========================================
        // 3. 补全常用的 KeyCode
        // ==========================================
        auto key = s_Data->LuaState["Key"].get_or_create<sol::table>();
        key["Space"] = Key::Space; // 跳跃必备！
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
        if (lsc.ScriptPath.empty()) return;

        // 为这个实体创建一个专属的 Lua Environment (沙盒)
        sol::environment& env = *(new sol::environment(s_Data->LuaState, sol::create, s_Data->LuaState.globals()));
        lsc.RuntimeEnvironment = &env;

        // 将 C++ 的 Entity 对象直接塞进这个沙盒中，名字叫 "entity"
        env["entity"] = entity;

        // 尝试加载并执行 Lua 文件
        try {
            s_Data->LuaState.script_file(lsc.ScriptPath, env);
            
            // 如果脚本里定义了 OnCreate 函数，调用它！
            sol::protected_function onCreateFunc = env["OnCreate"];
            if (onCreateFunc.valid()) {
                onCreateFunc();
            }
        } catch (const sol::error& err) {
            AYAYA_CORE_ERROR("Lua Error in {0}: {1}", lsc.ScriptPath, err.what());
        }
    }

    void ScriptEngine::OnUpdateEntity(Entity entity, Timestep ts) {
        auto& lsc = entity.GetComponent<LuaScriptComponent>();
        if (!lsc.RuntimeEnvironment) return;

        sol::environment& env = *(sol::environment*)lsc.RuntimeEnvironment;

        // 查找并调用 Lua 脚本里的 OnUpdate(ts)
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