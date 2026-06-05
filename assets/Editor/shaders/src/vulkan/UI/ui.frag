#version 450 core
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec4 v_Color;
layout(location = 1) in vec2 v_TexCoord;
layout(location = 2) in flat int v_TexIndex;

layout(set = 0, binding = 0) uniform sampler2D u_GlobalTextures[];

void main() {
    vec4 texColor = texture(u_GlobalTextures[nonuniformEXT(v_TexIndex)], v_TexCoord);
    FragColor = v_Color * texColor;
    // Pre-multiply alpha for correct ONE / ONE_MINUS_SRC_ALPHA blending.
    FragColor.rgb *= FragColor.a;
}
