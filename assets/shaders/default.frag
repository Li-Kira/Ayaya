#version 410 core

layout(location = 0) out vec4 color;
uniform vec4 u_Color; // 接收来自 C++ 的颜色

void main() {
    color = u_Color;
}