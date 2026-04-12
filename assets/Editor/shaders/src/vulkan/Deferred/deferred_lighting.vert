#version 450 core

layout(location = 0) out vec2 v_TexCoord;

void main() {
    // 【修复】：使用 gl_VertexIndex
    int vertexIndex = int(gl_VertexIndex);
    float x = -1.0 + float((vertexIndex & 1) << 2);
    float y = -1.0 + float((vertexIndex & 2) << 1);
    
    // 自动映射 UV 坐标到 [0, 1] 范围
    v_TexCoord = vec2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    
    // 直接输出屏幕裁剪坐标
    gl_Position = vec4(x, y, 0.0, 1.0);
}