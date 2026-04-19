#version 450 core
layout(location = 0) in vec3 a_Position;
layout(location = 0) out vec3 v_LocalPos;

// 【修正】：显式 alignas 或确保块大小是 16 的倍数
layout(push_constant) uniform Constants {
    mat4 u_Projection;
    mat4 u_View;
    float u_Roughness; 
    float _padding[3]; // 填充到 144 字节
} pc;

void main() {
    v_LocalPos = a_Position;  
    
    // 抹除平移
    mat4 rotView = mat4(mat3(pc.u_View));
    vec4 clipPos = pc.u_Projection * rotView * vec4(v_LocalPos, 1.0);
    
    // 强制深度为 1.0
    gl_Position = clipPos.xyww; 
}