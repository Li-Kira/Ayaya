#version 410 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent;

uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;
uniform mat4 u_NormalMatrix;
uniform mat4 u_TBNMatrix;

// Preview PBR push constants (passed as uniforms for OpenGL)
uniform vec4 u_Albedo;
uniform float u_Metallic;
uniform float u_Roughness;
uniform float u_AO;
uniform float u_Alpha;
uniform float u_AlphaCutoff;
uniform int u_BlendMode;
uniform int u_UseAlbedoMap;
uniform int u_UseNormalMap;
uniform int u_UseORMMap;
uniform int u_UseMetallicMap;
uniform int u_UseRoughnessMap;
uniform int u_UseAOMap;
uniform int u_EnableIBL;

out vec3 v_WorldPos;
out vec3 v_Normal;
out vec2 v_TexCoord;
out mat3 v_TBN;

void main() {
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_Normal = normalize(mat3(u_Transform) * a_Normal);
    v_TexCoord = a_TexCoord;

    // Build TBN
    vec3 T = normalize(mat3(u_Transform) * a_Tangent);
    vec3 N = normalize(mat3(u_Transform) * a_Normal);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    v_TBN = mat3(T, B, N);

    gl_Position = u_ViewProjection * worldPos;
}
