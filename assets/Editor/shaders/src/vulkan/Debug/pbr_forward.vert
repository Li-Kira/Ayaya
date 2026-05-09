#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent;

layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 CameraPosition;
} u_Camera;

layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
    vec4 Albedo;                   // offset 64
    int UseAlbedoMap;              // offset 80
    float Metallic;                // offset 84
    float Roughness;               // offset 88
    float AO;                      // offset 92
    int UseMetallicMap;            // offset 96
    int UseRoughnessMap;           // offset 100
    int UseAOMap;                  // offset 104
    int UseNormalMap;              // offset 108
    float EnvironmentIntensity;    // offset 112
    float _pad0;                   // offset 116
    float _pad1;                   // offset 120
    float _pad2;                   // offset 124
    vec4 EnvironmentAmbientColor;  // offset 128
} u_Push;

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;

void main() {
    vec4 worldPos = u_Push.ModelMatrix * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_Normal = mat3(u_Push.ModelMatrix) * a_Normal;
    v_TexCoord = a_TexCoord;
    gl_Position = u_Camera.ViewProjection * worldPos;
}
