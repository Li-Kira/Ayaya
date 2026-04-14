#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent; // 保证 44 字节顶点步长

// 【核心修复】：与 C++ 的 struct_CameraData 严丝合缝！
layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection; // 64 bytes
    vec3 CameraPosition; // 12 bytes
    // float _padding;   // GLSL 隐式对齐 4 bytes，总共 80 bytes
} u_Camera;

layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
    vec3 Albedo;
    int UseAlbedoMap;
} u_Push;

layout(location = 0) out vec2 v_TexCoord;

void main() {
    v_TexCoord = a_TexCoord;
    
    // 直接用组合好的 ViewProjection 矩阵！
    gl_Position = u_Camera.ViewProjection * u_Push.ModelMatrix * vec4(a_Position, 1.0);
}