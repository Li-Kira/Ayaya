#version 450 core

// 【核心改造】：明确 Location 输出和输入
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_ScreenTexture;

// 【核心改造】：放入 Push Constant
layout(push_constant) uniform Constants {
    vec2 u_TexelSize;
} pc;

float rgb2luma(vec3 rgb){
    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

void main() {
    vec3 colorCenter = texture(u_ScreenTexture, v_TexCoord).rgb;
    
    vec3 colorUp    = textureOffset(u_ScreenTexture, v_TexCoord, ivec2(0, 1)).rgb;
    vec3 colorDown  = textureOffset(u_ScreenTexture, v_TexCoord, ivec2(0, -1)).rgb;
    vec3 colorLeft  = textureOffset(u_ScreenTexture, v_TexCoord, ivec2(-1, 0)).rgb;
    vec3 colorRight = textureOffset(u_ScreenTexture, v_TexCoord, ivec2(1, 0)).rgb;

    float lumaCenter = rgb2luma(colorCenter);
    float lumaUp     = rgb2luma(colorUp);
    float lumaDown   = rgb2luma(colorDown);
    float lumaLeft   = rgb2luma(colorLeft);
    float lumaRight  = rgb2luma(colorRight);

    float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;

    if(lumaRange < max(0.0312, lumaMax * 0.125)) {
        FragColor = vec4(colorCenter, 1.0);
        return;
    }

    float lumaDownLeft  = rgb2luma(textureOffset(u_ScreenTexture, v_TexCoord, ivec2(-1, -1)).rgb);
    float lumaUpRight   = rgb2luma(textureOffset(u_ScreenTexture, v_TexCoord, ivec2(1, 1)).rgb);
    float lumaUpLeft    = rgb2luma(textureOffset(u_ScreenTexture, v_TexCoord, ivec2(-1, 1)).rgb);
    float lumaDownRight = rgb2luma(textureOffset(u_ScreenTexture, v_TexCoord, ivec2(1, -1)).rgb);

    float lumaDownUp = lumaDown + lumaUp;
    float lumaLeftRight = lumaLeft + lumaRight;

    float edgeHorz =  abs(-2.0 * lumaLeft + lumaDownLeft + lumaUpLeft)   + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0     + abs(-2.0 * lumaRight + lumaDownRight + lumaUpRight);
    float edgeVert =  abs(-2.0 * lumaUp + lumaUpLeft + lumaUpRight)      + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0  + abs(-2.0 * lumaDown + lumaDownLeft + lumaDownRight);

    bool isHorizontal = (edgeHorz >= edgeVert);

    float luma1 = isHorizontal ? lumaDown : lumaLeft;
    float luma2 = isHorizontal ? lumaUp : lumaRight;
    float gradient1 = abs(luma1 - lumaCenter);
    float gradient2 = abs(luma2 - lumaCenter);

    bool is1Steepest = gradient1 >= gradient2;
    
    // 【核心改造】：使用 pc.u_TexelSize 代替旧变量
    float stepLength = isHorizontal ? pc.u_TexelSize.y : pc.u_TexelSize.x;
    float lumaLocalAverage = 0.0;

    if(is1Steepest) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
    }

    vec2 currentUV = v_TexCoord;
    if(isHorizontal) currentUV.y += stepLength * 0.5;
    else currentUV.x += stepLength * 0.5;

    vec3 finalColor = texture(u_ScreenTexture, currentUV).rgb;
    FragColor = vec4(finalColor, 1.0);
}