#version 450 core

layout (location = 0) in vec3 a_Position;
layout(location = 0) out vec3 v_TexCoords;

// 【修复】：将顶点和片段需要的常量整合在一个结构里保证对齐
layout(push_constant) uniform Constants {
    mat4 u_Projection;
    mat4 u_View;
    mat4 u_Transform;
    float u_Intensity;
} pc;

void main() {
    v_TexCoords = a_Position; 
    
    vec4 pos = pc.u_Projection * pc.u_View * vec4(a_Position, 1.0);
    
    gl_Position = pos.xyww; 
}