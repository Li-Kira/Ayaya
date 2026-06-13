#version 410 core

layout(location = 0) in vec3 a_Position;

layout(std140) uniform Camera {
    mat4 u_ViewProjection;
    mat4 u_View;            // world→view
    vec3 u_CameraPos;
};
uniform mat4 u_Transform;

void main() {
    // 回归纯粹的矩阵乘法，所有的缩放由 C++ 的 AABB 算法精准控制
    gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
}