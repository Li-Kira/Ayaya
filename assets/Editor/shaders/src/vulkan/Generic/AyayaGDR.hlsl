// AyayaGDR.hlsl — Engine GDR Standard Library
// Include in custom shaders to hide SSBO vertex-pulling complexity.
// TA just calls GetAyayaVertex(vertexID, instanceID) → gets a ready-to-use vertex.

// ── GDR SSBO structs (must match C++ VulkanGeometryPool.hpp) ──

struct GPUInstance {
    float4x4 transform;       // offset 0,  size 64
    float4   boundingSphere;  // offset 64, size 16
    uint     geometryRangeIdx;// offset 80
    uint     materialIdx;     // offset 84
    uint     entityId;        // offset 88
    uint     flags;           // offset 92
};

struct GeometryRange {
    uint vertexOffset;        // uint element offset into g_Data[]
    uint indexOffset;         // byte offset for index buffer
    uint vertexCount;
    uint indexCount;
};

struct GPUMaterial {
    float4 albedo;
    float  metallic, roughness, ao, alpha;
    int    useAlbedoMap, useNormalMap, useORMMap;
    int    useMetallicMap, useRoughnessMap, useAOMap;
    int    albedoBindless, normalBindless, ormBindless;
    int    metallicBindless, roughnessBindless, aoBindless;
    float  alphaCutoff;
    int    blendMode;
    int    useAlphaMap;
    int    alphaBindless;
    uint   lightModeMask;
    uint   _pad0, _pad1, _pad2;       // alignas(16) padding
    float4 customData[4];             // TA-extensible: 64 bytes
};

// ── Set 2 bindings (shared GDR data) ──

[[vk::binding(0, 2)]] StructuredBuffer<GPUInstance>   u_Instances;
[[vk::binding(1, 2)]] StructuredBuffer<GeometryRange> u_Ranges;
[[vk::binding(2, 2)]] StructuredBuffer<GPUMaterial>   u_Materials;
[[vk::binding(3, 2)]] ByteAddressBuffer               g_Data; // raw geometry pool

// ── Engine vertex layout (11 uints = 44 bytes) ──
// position: float3 (uints 0-2), normal: float3 (3-5), uv: float2 (6-7), tangent: float3 (8-10)
static const uint kVertexStride = 11;

// ── TA-friendly output struct ──

struct AyayaVertex {
    float3 position;
    float3 normal;
    float2 uv;
    float4x4 worldMatrix;
    uint materialIdx;
};

// ── Core helper: TA calls ONE function to get a fully unpacked vertex ──

AyayaVertex GetAyayaVertex(uint vertexID, uint instanceID) {
    // CPU-sorted transparent path: override instanceID from push constant.
    // GDR path: overrideInstanceID == 0xFFFFFFFF → use SV_InstanceID.
    uint finalID = (pc.overrideInstanceID != 0xFFFFFFFFu) ? pc.overrideInstanceID : instanceID;
    GPUInstance inst = u_Instances[finalID];
    GeometryRange range = u_Ranges[inst.geometryRangeIdx];
    uint base = range.vertexOffset + vertexID * kVertexStride;

    // ByteAddressBuffer::Load(byteOffset) → uint4 at DWORD-aligned offset
    // Vertex layout: pos[3] | normal[3] | uv[2] | tangent[3] = 11 uints
    uint4 d0 = g_Data.Load(base * 4);        // {pos.x, pos.y, pos.z, normal.x}
    uint4 d1 = g_Data.Load(base * 4 + 12);   // {normal.y, normal.z, uv.x, uv.y}

    AyayaVertex v;
    v.position    = float3(asfloat(d0.x), asfloat(d0.y), asfloat(d0.z));
    v.normal      = float3(asfloat(d0.w), asfloat(d1.x), asfloat(d1.y));
    v.uv          = float2(asfloat(d1.z), asfloat(d1.w));
    v.worldMatrix = inst.transform;
    v.materialIdx = inst.materialIdx;
    return v;
}
