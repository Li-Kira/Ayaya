// depth_only.hlsl — Depth pre-pass: position output, minimal fragment shader
// Used with ColorWrite=false to produce depth-only rendering.
// Masked materials: discard pixels below alpha cutoff (prevents "cardboard cutout" artifact).
#include "Generic/AyayaGDR.hlsl"

struct VSOutput { float4 pos : SV_POSITION; nointerpolation uint matIdx : TEXCOORD0; };

#ifdef VERTEX_SHADER
VSOutput main(uint vID : SV_VertexID, uint iID : SV_InstanceID) {
    AyayaVertex v = GetAyayaVertex(vID, iID);
    VSOutput o;
    o.pos = mul(viewProj, mul(v.worldMatrix, float4(v.position, 1.0)));
    o.matIdx = v.materialIdx;
    return o;
}
#else
float4 main(VSOutput i) : SV_TARGET {
    // Masked discard: pixels below alphaCutoff must not write depth
    GPUMaterial mat = u_Materials[i.matIdx];
    if (mat.blendMode == 1) { // Masked
        if (mat.alpha < mat.alphaCutoff) discard;
    }
    return float4(0, 0, 0, 0); // ColorWrite=0 → GPU ignores this
}
#endif
