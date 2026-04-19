#version 450 core
layout(location = 0) out vec2 v_TexCoord;

void main() {
    // 魔法：仅通过 gl_VertexIndex 自动生成一个覆盖全屏幕的大三角形
    float x = -1.0 + float((gl_VertexIndex & 1) << 2);
    float y = -1.0 + float((gl_VertexIndex & 2) << 1);
    
    v_TexCoord.x = (x + 1.0) * 0.5;
    v_TexCoord.y = 1.0 - (y + 1.0) * 0.5;
    
    gl_Position = vec4(x, y, 0.0, 1.0);
}