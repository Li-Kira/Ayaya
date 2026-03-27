#version 330 core

in vec3 v_WorldPos;
out vec4 FragColor;

// 接收 C++ 传来的逆向曝光系数
uniform float u_ExposureInverse;

void main() {
    // 提取地面的 XZ 坐标
    vec2 coord = v_WorldPos.xz;

    // 获取屏幕空间偏导数，用于保持线宽恒定
    vec2 derivative = fwidth(coord);

    // ==========================================
    // 1. 双层网格计算
    // ==========================================
    // 1m 的细网格
    vec2 grid1 = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line1 = min(grid1.x, grid1.y);
    
    // 10m 的主网格
    vec2 grid10 = abs(fract(coord / 10.0 - 0.5) - 0.5) / (derivative / 10.0);
    float line10 = min(grid10.x, grid10.y);

    // 线条边缘抗锯齿混合值
    float alpha1 = 1.0 - min(line1, 1.0);
    float alpha10 = 1.0 - min(line10, 1.0);

    // ==========================================
    // 2. 颜色与粗细分配
    // ==========================================
    vec3 color = vec3(0.0);
    float finalAlpha = 0.0;

    vec3 colorGrid1 = vec3(0.15);  // 1m 网格的颜色
    vec3 colorGrid10 = vec3(0.35); // 10m 网格的颜色

    // 叠加逻辑：粗线覆盖细线
    if (alpha10 > 0.0) {
        color = colorGrid10;
        finalAlpha = alpha10;
    } else if (alpha1 > 0.0) {
        color = colorGrid1;
        finalAlpha = alpha1 * 0.4; // 1m 的细线透明度降到非常低，作为辅助
    }

    // ==========================================
    // 3. 轴线绘制
    // ==========================================
    float axisLineWidth = 1.2; // 轴线比普通网格稍粗一点点

    if (abs(v_WorldPos.z) < derivative.y * axisLineWidth) {
        color = vec3(0.70, 0.15, 0.15); // 砖红色 (X轴)
        finalAlpha = 1.0;
    } else if (abs(v_WorldPos.x) < derivative.x * axisLineWidth) {
        color = vec3(0.15, 0.15, 0.70); // 群青色 (Z轴)
        finalAlpha = 1.0;
    }

    // ==========================================
    // 4. 二次方平滑渐隐 (Smooth Squared Fade)
    // ==========================================
    // 将网格可见范围扩大到 100m
    float fade = max(0.0, 1.0 - length(v_WorldPos.xz) / 100.0);
    fade = fade * fade; // 核心魔法：平方衰减，让中心清晰，远端像起雾一样柔和消失

    // 结合曝光倍数输出，并设置整体最大透明度为 0.7
    FragColor = vec4(color * u_ExposureInverse, finalAlpha * fade * 0.7);

    // 性能优化：剔除完全看不见的像素
    if (FragColor.a < 0.01) {
        discard;
    }
}