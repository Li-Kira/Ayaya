#version 450 core

// Simple grayscale post-processing effect for GenericFullScreenPass.
// Reads u_Texture0 (bound to the upstream pass output) and converts to luminance.

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 out_Color;

layout(set = 1, binding = 0) uniform sampler2D u_Texture0;

void main() {
    vec2 uv = v_TexCoord;
    uv.y = 1.0 - uv.y;  // Vulkan Y-flip
    vec4 color = texture(u_Texture0, uv);
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    out_Color = vec4(vec3(gray), color.a);
}
