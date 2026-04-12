#version 450 core
layout(location = 0) in vec3 a_Position;
layout(location = 0) out vec3 v_LocalPos;

// 【核心改造】：将矩阵和 Roughness 统合在一个 Push Constant 块中
layout(push_constant) uniform Constants {
    mat4 u_Projection;
    mat4 u_View;
    float u_Roughness; // 占位，供给 prefilter.frag 使用
} pc;

void main() {
    v_LocalPos = a_Position;  
    
    // 抹除平移，只保留旋转，让相机永远在正中心
    mat4 rotView = mat4(mat3(pc.u_View));
    vec4 clipPos = pc.u_Projection * rotView * vec4(v_LocalPos, 1.0);
    
    // 深度魔法：强制让天空盒的深度为 1.0 (最远)
    gl_Position = clipPos.xyww; 
}