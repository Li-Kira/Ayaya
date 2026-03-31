#version 410 core
layout(location = 0) out vec4 o_Color;

in vec2 v_TexCoord;
uniform sampler2D u_Image;
uniform float u_FilterRadius;

vec3 SafeSample(vec2 uv) {
    vec3 c = texture(u_Image, uv).rgb;
    if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);
    return c;
}

void main() {
    float x = u_FilterRadius;
    float y = u_FilterRadius;

    // 3x3 帐篷滤波器 (Tent Filter)
    vec3 a = SafeSample(vec2(v_TexCoord.x - x, v_TexCoord.y + y));
    vec3 b = SafeSample(vec2(v_TexCoord.x,     v_TexCoord.y + y));
    vec3 c = SafeSample(vec2(v_TexCoord.x + x, v_TexCoord.y + y));

    vec3 d = SafeSample(vec2(v_TexCoord.x - x, v_TexCoord.y));
    vec3 e = SafeSample(vec2(v_TexCoord.x,     v_TexCoord.y));
    vec3 f = SafeSample(vec2(v_TexCoord.x + x, v_TexCoord.y));

    vec3 g = SafeSample(vec2(v_TexCoord.x - x, v_TexCoord.y - y));
    vec3 h = SafeSample(vec2(v_TexCoord.x,     v_TexCoord.y - y));
    vec3 i = SafeSample(vec2(v_TexCoord.x + x, v_TexCoord.y - y));

    vec3 color = e * 4.0;
    color += (b + d + f + h) * 2.0;
    color += (a + c + g + i);
    color *= 1.0 / 16.0;

    o_Color = vec4(color, 1.0);
}