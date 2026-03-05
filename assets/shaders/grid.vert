#version 330 core

layout(location = 0) in vec3 a_Position;

uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;

out vec3 v_WorldPos;

void main() {
    // 计算并向外传递像素在世界空间中的真实绝对坐标
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    gl_Position = u_ViewProjection * worldPos;
}