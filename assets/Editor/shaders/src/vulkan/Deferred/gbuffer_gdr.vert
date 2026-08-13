#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

// No VBO inputs — vertex data is pulled from SSBO via gl_VertexIndex

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;        // 0
    mat4 u_View;                  // 64
    vec3 u_CameraPosition;        // 128
    vec4 u_ScreenParams;          // 144
    vec4 u_Time;                  // 160
    mat4 u_PrevViewProjection;    // 176 — previous frame VP (motion vector)
};

// Instance data SSBO (set=2, binding=0)
struct GPUInstance {
    mat4 transform;        // 0   — current world
    mat4 prevTransform;    // 64  — previous frame world (motion vector)
    vec4 boundingSphere;   // 128
    uint geometryRangeIdx; // 144
    uint materialIdx;      // 148
    uint entityId;         // 152
    uint flags;            // 156
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
    int useAlphaMap;
    int alphaBindless;
    	uint lightModeMask;
	uint packing; uint _pad1, _pad2;  // align customData to 16-byte boundary (match C++ alignas(16))
	float customData[16];
};
// layout declared in fragment shader; vertex only needs materialIdx

layout(location = 0) out vec3 v_FragPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) flat out uint v_MaterialIdx;
layout(location = 4) flat out uint v_Flags;
layout(location = 5) out vec2 v_Velocity;  // screen-space motion (NDC → UV units)

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
    v_Flags = inst.flags;

    // ── Per-object motion vector (UE5-style): camera + object motion ──
    // Project both to UV space with the negative-viewport Y-flip, then take the
    // difference. X has no flip, but Y does: UV.y = 1.0 - (NDC.y * 0.5 + 0.5),
    // so motion.y must flip sign vs. NDC. Naive (ΔNDC) * 0.5 inverts the Y motion.
    vec4 currentClip = u_ViewProjection * worldPos;
    vec4 prevClip    = u_PrevViewProjection * inst.prevTransform * vec4(pos, 1.0);
    vec2 currentNDC  = currentClip.xy / currentClip.w;
    vec2 prevNDC     = prevClip.xy / prevClip.w;
    vec2 currentUV   = vec2(currentNDC.x * 0.5 + 0.5, 1.0 - (currentNDC.y * 0.5 + 0.5));
    vec2 prevUV      = vec2(prevNDC.x * 0.5 + 0.5, 1.0 - (prevNDC.y * 0.5 + 0.5));
    v_Velocity = currentUV - prevUV;  // screen-space motion in UV units

    gl_Position = currentClip;
}
