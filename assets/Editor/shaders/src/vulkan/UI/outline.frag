#version 450 core

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform Constants {
    mat4 u_Transform;
    vec3 u_Color;
} pc;

void main() {
    FragColor = vec4(pc.u_Color, 1.0);
}