#pragma once

// --- 标准库 ---
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

// --- 引擎核心基础 ---
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Timestep.hpp"

// --- 第三方库 ---
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// 根据平台包含特定头文件
#ifdef AYAYA_PLATFORM_WINDOWS
    #include <Windows.h>
#endif