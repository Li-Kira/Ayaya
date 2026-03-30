#version 410 core
out vec2 v_TexCoord;

void main() {
    // 魔法：仅通过 gl_VertexID 自动生成一个覆盖全屏幕的大三角形
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    v_TexCoord.x = (x + 1.0) * 0.5;
    v_TexCoord.y = (y + 1.0) * 0.5;
    gl_Position = vec4(x, y, 0.0, 1.0);
}