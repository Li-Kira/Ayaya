#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;

layout(location = 0) out vec4 v_Color;
layout(location = 1) out vec2 v_TexCoord;
layout(location = 2) out flat int v_TexIndex;

void main() {
    v_Color    = a_Color;
    v_TexCoord = a_TexCoord;
    v_TexIndex = int(a_TexIndex);
    gl_Position = vec4(a_Position, 1.0); // CPU pre-transformed to NDC
}
