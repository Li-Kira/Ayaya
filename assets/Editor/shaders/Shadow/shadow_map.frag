#version 410 core

void main() {
    // 故意留空！
    // 因为在这个 Pass 里，我们根本不绑定颜色缓冲 (Color Buffer)，
    // OpenGL 底层会自动把 gl_Position 的 Z 值写入到深度缓冲 (Depth Buffer) 中。
}