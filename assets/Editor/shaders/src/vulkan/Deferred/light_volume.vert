#version 450 core

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    mat4 u_View;
    vec3 u_CameraPosition;
};

// Instance world matrices [translate(pos) * scale(radius)]
layout(std430, set = 2, binding = 0) readonly buffer InstanceSSBO {
    mat4 u_InstanceWorld[];
};

// Global geometry pool — uint[] for vertex pulling
// Vertex layout (44B, 11 uint/vert): pos(3) + nrm(3) + uv(2) + tangent(3, skipped)
layout(std430, set = 2, binding = 2) readonly buffer GeometryPool {
    uint g_Data[];
};

// Single GeometryRange for the sphere mesh (rangeIdx=0 hardcoded)
struct GeometryRange {
    uint vertexOffset;
    uint indexOffset;
    uint vertexCount;
    uint indexCount;
};
layout(std430, set = 2, binding = 3) readonly buffer SphereRange {
    GeometryRange u_SphereRange;
};

layout(location = 0) flat out uint v_LightIndex;

const uint VERTEX_STRIDE = 11;  // 44 bytes / 4 = 11 uints per vertex

// Per-instance geometry range address (pushed via vertex pulling)
// The sphere mesh is always at the same range index for all instances,
// so we use a push constant to index into u_Ranges[].
layout(push_constant) uniform PC {
    mat4 u_InverseViewProj;
    vec4 u_ScreenParams;
} pc;

void main() {
    v_LightIndex = gl_InstanceIndex;

    uint base = u_SphereRange.vertexOffset + gl_VertexIndex * VERTEX_STRIDE;
    vec3 pos = vec3(
        uintBitsToFloat(g_Data[base + 0]),
        uintBitsToFloat(g_Data[base + 1]),
        uintBitsToFloat(g_Data[base + 2]));

    vec4 worldPos = u_InstanceWorld[gl_InstanceIndex] * vec4(pos, 1.0);
    gl_Position = u_ViewProjection * worldPos;
}
