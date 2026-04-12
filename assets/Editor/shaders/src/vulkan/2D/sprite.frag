#version 450 core

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

// 分配到 Set 1
layout(set = 1, binding = 0) uniform sampler2D u_Texture;

// 必须与 Vertex 保持结构完全一致
layout(push_constant) uniform Constants {
    mat4 u_Transform;
    vec4 u_Color;
    float u_ExposureInverse;
    int u_UseTexture;
} pc;

void main() {
    vec4 texColor = (pc.u_UseTexture == 1) ? texture(u_Texture, v_TexCoord) : vec4(1.0);
    vec4 finalColor = texColor * pc.u_Color;
    
    if (finalColor.a < 0.01) {
        discard;
    }

    FragColor = vec4(finalColor.rgb * pc.u_ExposureInverse, finalColor.a);
}