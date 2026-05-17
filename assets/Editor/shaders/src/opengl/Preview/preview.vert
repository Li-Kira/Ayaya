#version 410 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;

uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;
uniform mat4 u_NormalMatrix;

out vec3 v_Normal;
out vec3 v_WorldPos;

void main() {
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_Normal = normalize(mat3(u_NormalMatrix) * a_Normal);
    gl_Position = u_ViewProjection * worldPos;
}
