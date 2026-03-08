#version 410 core
layout (location = 0) in vec3 a_Position;

out vec3 v_TexCoords;

uniform mat4 u_Projection;
uniform mat4 u_View; // 注意：这里的 View 矩阵将被我们在 C++ 里抹除平移！

void main() {
    v_TexCoords = a_Position; // 天空盒的本地坐标直接就是 Cubemap 的 3D 采样向量！
    
    vec4 pos = u_Projection * u_View * vec4(a_Position, 1.0);
    
    // ==========================================
    // 核心优化魔法：让 Z 永远等于 W！
    // 这样在 GPU 进行透视除法 (xyz / w) 时，Z 就会变成 w/w = 1.0
    // 1.0 是 OpenGL 深度缓冲的最大值，这意味着天空盒永远在最远处！
    // ==========================================
    gl_Position = pos.xyww; 
}