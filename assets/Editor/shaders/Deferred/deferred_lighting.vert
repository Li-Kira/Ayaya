#version 410 core

out vec2 v_TexCoord;

void main() {
    // ==========================================
    // 现代 OpenGL 黑魔法：无需任何 VBO 顶点数据！
    // 利用 gl_VertexID (0, 1, 2) 直接算出覆盖全屏的大三角形坐标
    // ==========================================
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    
    // 自动映射 UV 坐标到 [0, 1] 范围
    v_TexCoord = vec2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    
    // 直接输出屏幕裁剪坐标
    gl_Position = vec4(x, y, 0.0, 1.0);
}