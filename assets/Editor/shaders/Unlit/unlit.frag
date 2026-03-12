#version 410 core

out vec4 FragColor;

in vec2 v_TexCoord;

uniform vec4 u_Color;      

uniform sampler2D u_AlbedoMap;
uniform bool u_UseAlbedoMap;

void main() {
    vec4 finalColor = u_Color;

    if (u_UseAlbedoMap) {
        // 注意：无光照材质通常不需要像 PBR 那样做复杂的 Gamma 逆校正，
        // 但如果你的引擎全局开启了 sRGB 帧缓冲，这里可以直接采样
        vec4 texColor = texture(u_AlbedoMap, v_TexCoord);
        finalColor *= texColor;
    }

    FragColor = finalColor;
}