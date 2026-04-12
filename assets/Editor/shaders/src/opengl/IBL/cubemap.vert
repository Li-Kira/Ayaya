#version 410 core
layout (location = 0) in vec3 a_Position;

uniform mat4 u_Projection;
uniform mat4 u_View;

out vec3 v_LocalPos;

void main() {
    v_LocalPos = a_Position;  
    // 抹除相机的平移，让相机永远呆在正中心
    mat4 rotView = mat4(mat3(u_View));
    gl_Position = u_Projection * rotView * vec4(v_LocalPos, 1.0);
}