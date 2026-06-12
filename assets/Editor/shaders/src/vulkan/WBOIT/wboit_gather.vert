#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent;

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    mat4 u_View;              // world→view (CameraData layout)
    vec3 u_CameraPosition;
};

layout(push_constant) uniform PC {
    mat4  u_Transform;
    vec4  u_Albedo;
    float u_Metallic;
    float u_Roughness;
    float u_AO;
    int   u_UseAlbedoMap;
    int   u_UseMetallicMap;
    int   u_UseRoughnessMap;
    int   u_UseAOMap;
    int   u_UseNormalMap;
    float u_Alpha;
} pc;

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;

void main() {
    vec4 worldPos = pc.u_Transform * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_Normal   = mat3(pc.u_Transform) * a_Normal;
    v_TexCoord = a_TexCoord;
    gl_Position = u_ViewProjection * worldPos;
}
