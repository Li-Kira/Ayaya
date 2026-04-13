#!/bin/bash

# ==========================================
# 编译 PostProcess Shader (Vulkan 专用源码 -> 通用 SPV 命名)
# ==========================================
glslc assets/Editor/shaders/PostProcess/postprocess.vulkan.vert -o assets/Editor/shaders/PostProcess/postprocess.vert.spv
glslc assets/Editor/shaders/PostProcess/postprocess.vulkan.frag -o assets/Editor/shaders/PostProcess/postprocess.frag.spv

echo "Shaders compiled successfully!"