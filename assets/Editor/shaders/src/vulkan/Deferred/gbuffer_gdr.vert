#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

// No VBO inputs — vertex data is pulled from SSBO via gl_VertexIndex

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPosition;
};

// Instance data SSBO (set=2, binding=0)
struct GPUInstance {
    mat4 transform;
    vec4 boundingSphere;
    uint geometryRangeIdx;
    uint materialIdx;
    uint entityId;
    uint flags;
};
layout(std430, set = 2, binding = 0) readonly buffer InstanceBuffer {
    GPUInstance Instances[];
};

// Geometry range SSBO (set=2, binding=1)
struct GeometryRange {
    uint vertexOffset;   // uint element offset into g_Data[] (byteOffset / 4)
    uint indexOffset;    // byte offset (for vkCmdBindIndexBuffer / firstIndex)
    uint vertexCount;
    uint indexCount;
};
layout(std430, set = 2, binding = 1) readonly buffer GeometryRangeBuffer {
    GeometryRange Ranges[];
};

// Global geometry buffer — raw uint[] for zero-padding vertex pulling
// Vertex layout (44 bytes packed, 11 uints per vertex):
//   [0]=pos.x, [1]=pos.y, [2]=pos.z, [3]=nrm.x, [4]=nrm.y, [5]=nrm.z,
//   [6]=uv.x,  [7]=uv.y,  [8..10]=tangent (skipped)
layout(std430, set = 2, binding = 3) readonly buffer GeometryBuffer {
    uint g_Data[];
};

// Material SSBO (set=2, binding=2) — accessed by fragment shader
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
// layout declared in fragment shader; vertex only needs materialIdx

layout(location = 0) out vec3 v_FragPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) flat out uint v_MaterialIdx;

const uint VERTEX_STRIDE = 11;  // 44 bytes / 4 = 11 uints per vertex

void main() {
    GPUInstance inst = Instances[gl_InstanceIndex];
    GeometryRange range = Ranges[inst.geometryRangeIdx];

    // SSBO vertex pulling — unpack Position + Normal + TexCoord, skip Tangent
    uint base = range.vertexOffset + gl_VertexIndex * VERTEX_STRIDE;
    vec3 pos = vec3(
        uintBitsToFloat(g_Data[base + 0]),
        uintBitsToFloat(g_Data[base + 1]),
        uintBitsToFloat(g_Data[base + 2]));
    vec3 nrm = vec3(
        uintBitsToFloat(g_Data[base + 3]),
        uintBitsToFloat(g_Data[base + 4]),
        uintBitsToFloat(g_Data[base + 5]));
    vec2 uv  = vec2(
        uintBitsToFloat(g_Data[base + 6]),
        uintBitsToFloat(g_Data[base + 7]));

    vec4 worldPos = inst.transform * vec4(pos, 1.0);
    v_FragPos = worldPos.xyz;
    v_Normal = mat3(inst.transform) * nrm;
    v_TexCoord = uv;
    v_MaterialIdx = inst.materialIdx;
    gl_Position = u_ViewProjection * worldPos;
}
