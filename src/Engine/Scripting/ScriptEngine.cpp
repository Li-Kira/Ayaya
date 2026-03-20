#include "ayapch.h"
#include "ScriptEngine.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Core/Input.hpp"

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
        // ==========================================
        // 魔法时刻：将 C++ 的 Transform 组件绑定到 Lua！
        // ==========================================
        
        // 1. 绑定 glm::vec3
        s_Data->LuaState.new_usertype<glm::vec3>("vec3",
            sol::constructors<glm::vec3(float, float, float)>(),
            "x", &glm::vec3::x,
            "y", &glm::vec3::y,
            "z", &glm::vec3::z
        );

        // 2. 绑定 TransformComponent
        s_Data->LuaState.new_usertype<TransformComponent>("TransformComponent",
            "Translation", &TransformComponent::Translation,
            "Rotation", &TransformComponent::Rotation,
            "Scale", &TransformComponent::Scale
        );

        // 3. 绑定 Entity 类，让 Lua 可以获取组件
        s_Data->LuaState.new_usertype<Entity>("Entity",
            // 绑定 GetComponent 方法 (专门为了 TransformComponent 实例)
            "GetTransform", [](Entity& e) -> TransformComponent& {
                return e.GetComponent<TransformComponent>();
            }
        );
    }

    void ScriptEngine::RegisterAPI() {
        // 暴露 Input 系统给 Lua
        auto input = s_Data->LuaState["Input"].get_or_create<sol::table>();
        input.set_function("IsKeyPressed", &Input::IsKeyPressed);
        
        // 暴露按键枚举 (你可以把常用的键都在这里注册)
        auto key = s_Data->LuaState["Key"].get_or_create<sol::table>();
        key["W"] = Key::W;
        key["A"] = Key::A;
        key["S"] = Key::S;
        key["D"] = Key::D;
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