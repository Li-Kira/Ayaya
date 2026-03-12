#version 410 core

layout (location = 0) in vec3 a_Position;
layout (location = 2) in vec2 a_TexCoord;

out vec2 v_TexCoord;

// 摄像机矩阵
layout(std140) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPos;
};

uniform mat4 u_Transform;

void main() {
    v_TexCoord = a_TexCoord;
    gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
}