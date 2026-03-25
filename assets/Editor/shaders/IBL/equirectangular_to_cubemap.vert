#version 410 core
layout (location = 0) in vec3 a_Position;

uniform mat4 u_Projection;
uniform mat4 u_View;

out vec3 v_LocalPos;

void main() {
    v_LocalPos = a_Position;  
    // 抹除平移，只保留旋转，让相机永远在正中心
    mat4 rotView = mat4(mat3(u_View));
    vec4 clipPos = u_Projection * rotView * vec4(v_LocalPos, 1.0);
    
    gl_Position = clipPos.xyww; // 深度魔法：强制让天空盒的深度为 1.0 (最远)
}