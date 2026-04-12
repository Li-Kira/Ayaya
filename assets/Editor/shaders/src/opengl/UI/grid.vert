#version 330 core

layout(location = 0) in vec3 a_Position;

layout(std140) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPos;
};
uniform mat4 u_Transform;

out vec3 v_WorldPos;

void main() {
    // 计算并向外传递像素在世界空间中的真实绝对坐标
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    gl_Position = u_ViewProjection * worldPos;
}