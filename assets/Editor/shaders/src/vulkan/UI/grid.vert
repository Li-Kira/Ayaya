#version 450 core

layout(location = 0) in vec3 a_Position;

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPosition; // 注意：为了匹配 C++ 里的 CameraData，这里统一用 u_CameraPosition
};

layout(push_constant) uniform Constants {
    mat4 u_Transform;
    float u_ExposureInverse;
} pc;

layout(location = 0) out vec3 v_WorldPos;

void main() {
    vec4 worldPos = pc.u_Transform * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    gl_Position = u_ViewProjection * worldPos;
}