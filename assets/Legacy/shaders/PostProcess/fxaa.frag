#version 410 core
out vec4 FragColor;
in vec2 v_TexCoord;

uniform sampler2D u_ScreenTexture;
uniform vec2 u_TexelSize;

// 亮度转换公式
float rgb2luma(vec3 rgb){
    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

void main() {
    vec3 colorCenter = texture(u_ScreenTexture, v_TexCoord).rgb;
    
    // 1. 获取周围四个方向的像素颜色
    vec3 colorUp    = textureOffset(u_ScreenTexture, v_TexCoord, ivec2(0, 1)).rgb;
    vec3 colorDown  = textureOffset(u_ScreenTexture, v_TexCoord, ivec2(0, -1)).rgb;
    vec3 colorLeft  = textureOffset(u_ScreenTexture, v_TexCoord, ivec2(-1, 0)).rgb;
    vec3 colorRight = textureOffset(u_ScreenTexture, v_TexCoord, ivec2(1, 0)).rgb;

    // 2. 转换为亮度 (Luma)
    float lumaCenter = rgb2luma(colorCenter);
    float lumaUp     = rgb2luma(colorUp);
    float lumaDown   = rgb2luma(colorDown);
    float lumaLeft   = rgb2luma(colorLeft);
    float lumaRight  = rgb2luma(colorRight);

    // 3. 计算对比度，判断是否是边缘
    float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;

    // 如果对比度太小（不是边缘），直接输出原色并退出，这是 FXAA 极快的原因！
    if(lumaRange < max(0.0312, lumaMax * 0.125)) {
        FragColor = vec4(colorCenter, 1.0);
        return;
    }

    // 4. 计算四个对角线的亮度
    float lumaDownLeft  = rgb2luma(textureOffset(u_ScreenTexture, v_TexCoord, ivec2(-1, -1)).rgb);
    float lumaUpRight   = rgb2luma(textureOffset(u_ScreenTexture, v_TexCoord, ivec2(1, 1)).rgb);
    float lumaUpLeft    = rgb2luma(textureOffset(u_ScreenTexture, v_TexCoord, ivec2(-1, 1)).rgb);
    float lumaDownRight = rgb2luma(textureOffset(u_ScreenTexture, v_TexCoord, ivec2(1, -1)).rgb);

    // 5. 确定是水平边缘还是垂直边缘
    float lumaDownUp = lumaDown + lumaUp;
    float lumaLeftRight = lumaLeft + lumaRight;

    float edgeHorz =  abs(-2.0 * lumaLeft + lumaDownLeft + lumaUpLeft)   + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0     + abs(-2.0 * lumaRight + lumaDownRight + lumaUpRight);
    float edgeVert =  abs(-2.0 * lumaUp + lumaUpLeft + lumaUpRight)      + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0  + abs(-2.0 * lumaDown + lumaDownLeft + lumaDownRight);

    bool isHorizontal = (edgeHorz >= edgeVert);

    // 6. 根据边缘方向进行定向的像素混合 (模糊锯齿)
    float luma1 = isHorizontal ? lumaDown : lumaLeft;
    float luma2 = isHorizontal ? lumaUp : lumaRight;
    float gradient1 = abs(luma1 - lumaCenter);
    float gradient2 = abs(luma2 - lumaCenter);

    bool is1Steepest = gradient1 >= gradient2;
    float gradientScaled = 0.25 * max(gradient1, gradient2);

    float stepLength = isHorizontal ? u_TexelSize.y : u_TexelSize.x;
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

    // 最终采样抗锯齿后的颜色
    vec3 finalColor = texture(u_ScreenTexture, currentUV).rgb;
    FragColor = vec4(finalColor, 1.0);
}