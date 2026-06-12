#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPosition;
};

// Instance data SSBO (set=2, binding=0 — set=1 is fragment textures)
struct GPUInstance {
    mat4 transform;
    vec4 boundingSphere;
    uint geometryRangeIdx;
    uint materialIdx;
    uint entityId;
    uint _pad;
};
layout(std430, set = 2, binding = 0) readonly buffer InstanceBuffer {
    GPUInstance Instances[];
};

// Geometry range SSBO (set=2, binding=1)
struct GeometryRange {
    uint vertexOffset;
    uint indexOffset;
    uint vertexCount;
    uint indexCount;
};
layout(std430, set = 2, binding = 1) readonly buffer GeometryRangeBuffer {
    GeometryRange Ranges[];
};

// Material SSBO (set=2, binding=2)
struct GPUMaterial {
    vec4 albedo;
    float metallic, roughness, ao, alpha;
    int useAlbedoMap, useNormalMap, useORMMap;
    int useMetallicMap, useRoughnessMap, useAOMap;
    int albedoBindless, normalBindless, ormBindless;
    int metallicBindless, roughnessBindless, aoBindless;
    float alphaCutoff;
    int blendMode;
    int _pad[4];
};
layout(std430, set = 2, binding = 2) readonly buffer MaterialBuffer {
    GPUMaterial Materials[];
};

// No push constants for MDI — instance index comes from gl_BaseInstance
// (set via firstInstance field in VkDrawIndexedIndirectCommand)

layout(location = 0) out vec3 v_FragPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) flat out uint v_MaterialIdx;

void main() {
    // gl_InstanceIndex = firstInstance (set per indirect command) + 0 (instanceCount=1)
    GPUInstance inst = Instances[gl_InstanceIndex];
    GPUMaterial mat = Materials[inst.materialIdx];

    vec4 worldPos = inst.transform * vec4(a_Position, 1.0);
    v_FragPos = worldPos.xyz;
    mat3 normalMatrix = transpose(inverse(mat3(inst.transform)));
    v_Normal = normalMatrix * a_Normal;
    v_TexCoord = a_TexCoord;
    v_MaterialIdx = inst.materialIdx;
    gl_Position = u_ViewProjection * worldPos;
}
