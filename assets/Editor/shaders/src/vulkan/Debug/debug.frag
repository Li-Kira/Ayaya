#version 450 core
layout(location = 0) out vec4 o_Color;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;

layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
    vec3 Albedo;
    int UseAlbedoMap;
} u_Push;

void main() {
    // 强制输出亮绿色来确认 Pass 是否在工作
    o_Color = vec4(0.0, 1.0, 0.0, 1.0); 
    
    // vec4 tex = (u_Push.UseAlbedoMap == 1) ? texture(u_AlbedoMap, v_TexCoord) : vec4(1.0);
    // o_Color = vec4(tex.rgb * u_Push.Albedo, 1.0);
}