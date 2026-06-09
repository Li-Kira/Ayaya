#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent;

layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
    vec3 CameraPosition;
} u_Camera;

layout(push_constant) uniform PushData {
    mat4 ModelMatrix;
    vec4 Albedo;
    float Metallic;
    float Roughness;
    float AO;
    float Alpha;
    float AlphaCutoff;
    int BlendMode;           // 0=Opaque, 1=Masked
    int UseAlbedoMap;
    int UseNormalMap;
    int UseORMMap;
    int UseMetallicMap;
    int UseRoughnessMap;
    int UseAOMap;
    int EnableIBL;
} u_Push;

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) out mat3 v_TBN;

void main() {
    vec4 worldPos = u_Push.ModelMatrix * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;

    mat3 normalMatrix = mat3(u_Push.ModelMatrix);
    v_Normal = normalize(normalMatrix * a_Normal);
    v_TexCoord = a_TexCoord;

    // Build TBN for normal mapping
    vec3 T = normalize(normalMatrix * a_Tangent);
    vec3 N = normalize(normalMatrix * a_Normal);
    T = normalize(T - dot(T, N) * N);  // Gram-Schmidt re-orthogonalize
    vec3 B = cross(N, T);
    v_TBN = mat3(T, B, N);

    gl_Position = u_Camera.ViewProjection * worldPos;
}
