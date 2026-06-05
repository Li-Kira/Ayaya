#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_CustomData;

void main() {
    float selected = texture(u_CustomData, v_TexCoord).g;
    FragColor = vec4(selected, 0.0, 0.0, 1.0);
}
