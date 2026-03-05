#version 330 core

in vec3 v_WorldPos;
out vec4 FragColor;

void main() {
    // 提取地面的 XZ 坐标 (Y永远为0)
    vec2 coord = v_WorldPos.xz;

    // fwidth 偏导数魔法：保证网格线在任何缩放级别下都是 1 像素宽的丝滑线条
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    float line = min(grid.x, grid.y);
    float alpha = 1.0 - min(line, 1.0);

    // Blender 风格：X 轴是红色，Z 轴是蓝色
    vec3 color = vec3(0.4); // 默认灰色线条
    if (abs(v_WorldPos.z) < 0.02) {
        color = vec3(0.8, 0.2, 0.2); // X 轴红线
    } else if (abs(v_WorldPos.x) < 0.02) {
        color = vec3(0.2, 0.2, 0.8); // Z 轴蓝线
    }

    // 核心魔法：距离渐隐 (Fade Out)。离原点越远越透明，营造无限延伸的感觉，且防止远处摩尔纹！
    float fade = max(0.0, 1.0 - length(v_WorldPos.xz) / 50.0); // 50.0 是网格的可见半径

    FragColor = vec4(color, alpha * fade * 0.6); // 0.6 是整体透明度

    // 剔除掉完全透明的像素，大幅节约显卡性能
    if (FragColor.a < 0.01) {
        discard;
    }
}