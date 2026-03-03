#pragma once

/**
 * Ayaya Engine - 核心公共头文件
 * 客户端项目 (如 Sandbox) 只需要包含此文件即可访问引擎功能
 */

// --- 基础层 (Core) ---
#include "Core/Log.hpp"            // 日志系统 (AYAYA_CORE_INFO, etc.)
#include "Core/Application.hpp"    // 应用框架
#include "Core/Layer.hpp"          // 逻辑层基类
#include "Core/Timestep.hpp"       // 时间步 (Delta Time)
#include "Core/Input.hpp"          // 输入系统
#include "Core/KeyCodes.hpp"       // 按键码映射

// --- 渲染层 (Renderer) ---
#include "Renderer/Shader.hpp"     // 着色器类

// --- 第三方库集成 (可选，视需求暴露) ---
// 如果你希望在客户端直接使用 GLM 数学库
// #include <glm/glm.hpp>
// #include <glm/gtc/matrix_transform.hpp>
// #include <glm/gtc/type_ptr.hpp>

// --- 常用标准库 ---
#include <memory>
#include <string>
#include <vector>
#include <iostream>