#pragma once
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include <string>

namespace Ayaya {

    class ScriptEngine {
    public:
        static void Init();
        static void Shutdown();

        // 当游戏开始时，为挂载了 Lua 脚本的实体创建运行环境
        static void OnCreateEntity(Entity entity);
        // 每帧调用
        static void OnUpdateEntity(Entity entity, Timestep ts);

    private:
        // 核心绑定函数：在这里教 Lua 认识 C++ 的 Transform、Input 等
        static void RegisterComponents();
        static void RegisterAPI();
    };

}