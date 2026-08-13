// ==========================================
// AyayaGDR.hlsl — Engine GDR Standard Library v2
//
// TA 只需 #include this file，自动获得:
//   Camera UBO (Set 0): viewProj, view, cameraPos, _ScreenParams, _Time
//   Push Constants:     pc.planes[], pc.overrideInstanceID, pc._TexelSize
//   GDR SSBO (Set 2):   u_Instances, u_Ranges, u_Materials, g_Data
//   GetAyayaVertex(vertexID, instanceID)
//
// TA 永远不需要自己声明 cbuffer 或 push constant。
// ==========================================

// ── Camera UBO (Set 0 Binding 0, 176 bytes, matches C++ struct_CameraData) ──
[[vk::binding(0, 0)]] cbuffer CameraUBO {
    float4x4 viewProj;       // offset 0
    float4x4 view;           // offset 64  — world→view
    float3   cameraPos;      // offset 128
    float    _pad0;          // offset 140
    float4   _ScreenParams;  // offset 144 — x=w, y=h, z=1+1/w, w=1+1/h
    float4   _Time;          // offset 160 — x=t/20, y=t, z=t*2, w=t*3
};

// ── Frame constant access macros ──
#define AYAYA_TIME           _Time.y
#define AYAYA_DELTA          _Time.x
#define AYAYA_SCREEN_W       _ScreenParams.x
#define AYAYA_SCREEN_H       _ScreenParams.y

// ── Push Constants (matches C++ FrustumPush) ──
struct FrustumPC {
    float4 planes[6];           // offset 0,  size 96
    uint   instanceCount;       // offset 96, size 4
    uint   lightModeMask;       // offset 100, size 4
    uint   overrideInstanceID;  // offset 104, size 4 — GDR: 0xFFFFFFFF → use SV_InstanceID
    uint   _pad;               // offset 108, size 4
    float4 _TexelSize;         // offset 112, size 16 — x=1/tw, y=1/th, z=tw, w=th
    float  _ExposureInverse;   // offset 128, size 4 — 1.0/PhysicalExposure (matches ForwardBlend)
};
[[vk::push_constant]] FrustumPC pc;

// ── GDR SSBO structs (must match C++ VulkanGeometryPool.hpp) ──

struct GPUInstance {
    float4x4 transform;       // offset 0,  size 64 — current world
    float4x4 prevTransform;   // offset 64, size 64 — previous frame world (motion vector)
    float4   boundingSphere;  // offset 128, size 16
    uint     geometryRangeIdx;// offset 144
    uint     materialIdx;     // offset 148
    uint     entityId;        // offset 152
    uint     flags;           // offset 156
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
    uint   packing; uint _pad1, _pad2; // TexturePacking enum + alignas(16)
    float4 customData[4];             // TA-extensible: 64 bytes
};

// ── Set 2 bindings (shared GDR data) ──

[[vk::binding(0, 2)]] StructuredBuffer<GPUInstance>   u_Instances;
[[vk::binding(1, 2)]] StructuredBuffer<GeometryRange> u_Ranges;
[[vk::binding(2, 2)]] StructuredBuffer<GPUMaterial>   u_Materials;
[[vk::binding(3, 2)]] StructuredBuffer<uint>           g_Data; // raw geometry pool (StructuredBuffer for MoltenVK compat)

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

    // StructuredBuffer<uint> index-based access (MoltenVK-compatible).
    // Vertex layout: pos[3] | normal[3] | uv[2] | tangent[3] = 11 uints
    uint4 d0 = uint4(g_Data[base+0], g_Data[base+1], g_Data[base+2], g_Data[base+3]); // {pos.x, pos.y, pos.z, normal.x}
    uint4 d1 = uint4(g_Data[base+4], g_Data[base+5], g_Data[base+6], g_Data[base+7]); // {normal.y, normal.z, uv.x, uv.y}

    AyayaVertex v;
    v.position    = float3(asfloat(d0.x), asfloat(d0.y), asfloat(d0.z));
    v.normal      = float3(asfloat(d0.w), asfloat(d1.x), asfloat(d1.y));
    v.uv          = float2(asfloat(d1.z), asfloat(d1.w));
    v.worldMatrix = inst.transform;
    v.materialIdx = inst.materialIdx;
    return v;
}
