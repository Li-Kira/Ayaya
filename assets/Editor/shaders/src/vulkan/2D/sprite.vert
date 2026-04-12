#version 450 core

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPosition;
};

// 【核心改造】：整合所有 Sprite 需要的参数
layout(push_constant) uniform Constants {
    mat4 u_Transform;
    vec4 u_Color;
    float u_ExposureInverse;
    int u_UseTexture;
} pc;

layout(location = 0) out vec2 v_TexCoord;

void main() {
    // 核心魔法：凭空生成 1x1 的完美 2D 矩形
    vec2 pos[4] = vec2[4](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2(-0.5,  0.5),
        vec2( 0.5,  0.5)
    );
    vec2 tex[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0)
    );
    
    int idx = int(gl_VertexIndex);
    v_TexCoord = tex[idx];
    
    gl_Position = u_ViewProjection * pc.u_Transform * vec4(pos[idx], 0.0, 1.0);
}