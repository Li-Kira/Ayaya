#version 450 core

layout(location = 0) out vec4 FragColor;

void main() {
    // 这个超级亮的洋红色会在后处理 Bloom 里亮瞎眼，能第一时间提醒你模型材质丢了
    FragColor = vec4(100000.0, 0.0, 100000.0, 1.0);
}