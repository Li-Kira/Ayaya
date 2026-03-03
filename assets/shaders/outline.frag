#version 410 core

layout(location = 0) out vec4 FragColor;

uniform vec3 u_Color; // 接收传进来的描边颜色

void main() {
    FragColor = vec4(u_Color, 1.0);
}