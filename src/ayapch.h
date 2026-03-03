#pragma once

// --- 只有 C++ 编译时才包含的头文件 ---
#ifdef __cplusplus
    #include <iostream>
    #include <memory>
    #include <utility>
    #include <algorithm>
    #include <functional>
    #include <string>
    #include <sstream>
    #include <vector>
    #include <unordered_map>
    #include <unordered_set>

    // 引擎内部常用的 C++ 头文件
    #include "Engine/Core/Log.hpp"
    #include "Engine/Core/Timestep.hpp"
    
    // 第三方 C++ 库
    #include <glm/glm.hpp>
    #include <glm/gtc/matrix_transform.hpp>

    #include <imgui.h>
    #include <imgui_internal.h>
    #include <imgui_impl_glfw.h>
    #include <imgui_impl_opengl3.h>
#endif

// --- C 和 C++ 都能包含的头文件 (如 GLAD 和 GLFW) ---
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#ifdef AYAYA_PLATFORM_WINDOWS
    #include <Windows.h>
#endif