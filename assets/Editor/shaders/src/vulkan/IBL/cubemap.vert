#version 450 core
layout(location = 0) in vec3 a_Position;
layout(location = 0) out vec3 v_LocalPos;

// 【修复 1】：必须与 prefilter.frag 和 C++ 的结构完全一致！
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
    
    // 【修复 2】：退后一点点，防止 Vulkan 远裁剪面误杀 1.0
    gl_Position = vec4(clipPos.xy, clipPos.w * 0.99999, clipPos.w); 
}