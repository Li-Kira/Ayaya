#version 450 core

// 全屏三角形 — 完全依赖 gl_VertexIndex，无需任何顶点缓冲区
layout(location = 0) out vec2 v_TexCoords;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 uvs[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(2.0, 0.0),
        vec2(0.0, 2.0)
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    v_TexCoords = uvs[gl_VertexIndex];
}
