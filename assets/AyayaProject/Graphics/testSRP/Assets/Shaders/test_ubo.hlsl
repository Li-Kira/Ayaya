struct VSInput { uint vertexID : SV_VertexID; };
struct PSInput { float4 position : SV_POSITION; float2 texCoord : TEXCOORD0; };

#ifdef VERTEX_SHADER
void main(VSInput input, out PSInput output) { /* ... same ... */
    float2 pos[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };
    float2 uv[3]  = { float2(0,0), float2(2,0), float2(0,2) };
    output.position = float4(pos[input.vertexID], 0, 1);
    output.texCoord = uv[input.vertexID];
}
#else
[[vk::combinedImageSampler]][[vk::binding(0, 1)]]
Texture2D u_Texture0 : register(t0, space1);
[[vk::combinedImageSampler]][[vk::binding(0, 1)]]
SamplerState u_Sampler0 : register(s0, space1);

[[vk::binding(2, 0)]] cbuffer MaterialData : register(b2) {
    float _EdgeThickness;
    float4 _GlowColor;
    float _ScanSpeed;
};

float4 main(PSInput i) : SV_TARGET {
    float2 uv = i.texCoord; uv.y = 1.0 - uv.y;
    return u_Texture0.Sample(u_Sampler0, uv) * _GlowColor;
}
#endif
