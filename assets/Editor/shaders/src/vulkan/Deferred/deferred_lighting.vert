#version 450 core

layout(push_constant) uniform PC {
    mat4 u_LightSpaceMatrix;
    vec3 u_AmbientColor; float u_Intensity;
    int u_EnvMapEnabled; int u_EnableSSAO;
    mat4 u_InverseViewProj;   // offset 96: combined NDC→world
} pc;

layout(location = 0) out vec2 v_TexCoord;

void main() {
    // OpenGL NDC (Y-up) for negative-height viewport.
    float x = -1.0 + float((gl_VertexIndex & 1) << 2);     // {-1, 3, -1}
    float y =  1.0 - float((gl_VertexIndex & 2) << 1);     // { 1, 1, -3}
    v_TexCoord = vec2((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
