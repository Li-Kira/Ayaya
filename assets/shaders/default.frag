#version 410 core

layout(location = 0) out vec4 color;

in vec3 v_Color;
in vec2 v_TexCoord;

uniform sampler2D u_Texture; // 对应 ExampleLayer 中的 shader->SetInt("u_Texture", 0)

void main() {
    // 将贴图采样结果与顶点颜色相乘
    // 如果只想看贴图，可以直接使用 texture(u_Texture, v_TexCoord)
    color = texture(u_Texture, v_TexCoord) * vec4(v_Color, 1.0);
}