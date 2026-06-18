#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent;

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    mat4 u_View;
    vec3 u_CameraPosition;
};

// Instance transforms in a dedicated descriptor set (set=2, binding=0)
layout(std430, set = 2, binding = 0) readonly buffer InstanceBuffer {
    mat4 Transforms[];
};

layout(push_constant) uniform PC {
    mat4   u_Transform;                        // offset 0   (64B)
    vec4   u_Albedo;                           // offset 64  (16B)
    float  u_Metallic;                         // offset 80  (4B)
    float  u_Roughness;                        // offset 84  (4B)
    float  u_AO;                               // offset 88  (4B)
    uint   u_UseORMMap;                        // offset 92  (4B)
    uint   u_AlbedoMapIndex;                   // offset 96  (4B)
    uint   u_NormalMapIndex;                   // offset 100 (4B)
    uint   u_ORMMapIndex;                      // offset 104 (4B)
    uint   u_MetallicMapIndex;                 // offset 108 (4B)
    uint   u_RoughnessMapIndex;                // offset 112 (4B)
    uint   u_AOMapIndex;                       // offset 116 (4B)
    float  u_Alpha;                            // offset 120 (4B)
} pc;

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;

void main() {
    mat4 transform = Transforms[gl_InstanceIndex];
    vec4 worldPos = transform * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_Normal   = mat3(transform) * a_Normal;
    v_TexCoord = a_TexCoord;
    gl_Position = u_ViewProjection * worldPos;
}
