#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    mat4 u_View;              // world→view
    vec3 u_CameraPosition;
};

// Instance transforms in a dedicated descriptor set (set=2, binding=0)
layout(std430, set = 2, binding = 0) readonly buffer InstanceBuffer {
    mat4 Transforms[];
};

layout(push_constant) uniform PushConstants {
    mat4 u_Transform;       // ignored in instanced path
    vec3 u_Albedo;
    float u_ReceiveShadows;
    float u_Metallic;
    float u_Roughness;
    float u_AO;
    float u_AlphaMultiplier;
    float u_AlphaCutoff;
    int   u_BlendMode;
    int u_UseAlbedoMap;
    int u_UseMetallicMap;
    int u_UseRoughnessMap;
    int u_UseAOMap;
    int u_UseNormalMap;
    int u_UseAlphaMap;
    int u_IsSelected;
} pc;

layout(location = 0) out vec3 v_FragPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;

void main() {
    mat4 transform = Transforms[gl_InstanceIndex];
    vec4 worldPos = transform * vec4(a_Position, 1.0);
    v_FragPos = worldPos.xyz;
    mat3 normalMatrix = transpose(inverse(mat3(transform)));
    v_Normal = normalMatrix * a_Normal;
    v_TexCoord = a_TexCoord;
    gl_Position = u_ViewProjection * worldPos;
}
