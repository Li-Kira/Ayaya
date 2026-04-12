#version 450 core

layout(location = 0) in vec3 a_Position;

// 【核心改造】：将零散的矩阵统一打包，供 C++ 端一次性推送
layout(push_constant) uniform Constants {
    mat4 u_LightSpaceMatrix;
    mat4 u_Transform;
} pc;

void main() {
    gl_Position = pc.u_LightSpaceMatrix * pc.u_Transform * vec4(a_Position, 1.0);
}