#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;

layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 CameraPosition;
} u_Camera;

layout(push_constant) uniform PushData {
    mat4 ModelMatrix;   // offset 0,  size 64
    vec4 Albedo;         // offset 64, size 16
    vec4 LightDir;       // offset 80, size 16
    vec4 LightColor;     // offset 96, size 16
    vec4 Ambient;        // offset 112, size 16
} u_Push;

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;

void main() {
    vec4 worldPos = u_Push.ModelMatrix * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_Normal = normalize(mat3(u_Push.ModelMatrix) * a_Normal);
    gl_Position = u_Camera.ViewProjection * worldPos;
}
