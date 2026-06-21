#version 450 core

layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform Constants {
    mat4 u_LightSpaceMatrix;
    mat4 u_Transform;
} pc;

void main() {
    gl_Position = pc.u_LightSpaceMatrix * pc.u_Transform * vec4(a_Position, 1.0);
}
