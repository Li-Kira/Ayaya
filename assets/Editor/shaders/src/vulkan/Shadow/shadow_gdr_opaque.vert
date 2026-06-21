#version 450 core

// No VBO inputs — SSBO vertex pulling from shared GDRContext
// Opaque variant: unpacks position only (3 uints), skips normal/UV/tangent
// Used with VS-only pipeline (NoFragmentShader=true) for maximum Early-Z performance

// set=2, binding=0: shared GPUInstance[] SSBO
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

// set=2, binding=1: shared GeometryRange[] SSBO
struct GeometryRange {
    uint vertexOffset;
    uint indexOffset;
    uint vertexCount;
    uint indexCount;
};
layout(std430, set = 2, binding = 1) readonly buffer GeometryRangeBuffer {
    GeometryRange Ranges[];
};

// set=2, binding=3: shared vertex geometry pool (uint g_Data[])
layout(std430, set = 2, binding = 3) readonly buffer GeometryBuffer {
    uint g_Data[];
};

const uint VERTEX_STRIDE = 11;  // 44 bytes / 4 = 11 uints per vertex

layout(push_constant) uniform Constants {
    mat4 u_LightSpaceMatrix;
} pc;

void main() {
    GPUInstance inst = Instances[gl_InstanceIndex];
    GeometryRange range = Ranges[inst.geometryRangeIdx];

    // SSBO vertex pulling — unpack Position only (3 uints)
    uint base = range.vertexOffset + gl_VertexIndex * VERTEX_STRIDE;
    vec3 pos = vec3(
        uintBitsToFloat(g_Data[base + 0]),
        uintBitsToFloat(g_Data[base + 1]),
        uintBitsToFloat(g_Data[base + 2]));

    vec4 worldPos = inst.transform * vec4(pos, 1.0);
    gl_Position = pc.u_LightSpaceMatrix * worldPos;
}
