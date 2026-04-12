#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoords;

// 规范输出 Location
layout(location = 0) out vec2 v_TexCoords;

void main() {
    v_TexCoords = a_TexCoords;
    gl_Position = vec4(a_Position, 1.0);
}