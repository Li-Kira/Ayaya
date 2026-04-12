#version 450 core

layout(location = 0) in vec3 a_Position;

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPosition;
};

layout(push_constant) uniform Constants {
    mat4 u_Transform;
    vec3 u_Color;
} pc;

void main() {
    gl_Position = u_ViewProjection * pc.u_Transform * vec4(a_Position, 1.0);
}