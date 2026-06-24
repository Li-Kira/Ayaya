// grayscale.hlsl — TA-authored HLSL post-processing shader
// Compile via editor GUI: ContentBrowser → select .hlsl → "Compile HLSL"

struct VSInput { uint vertexID : SV_VertexID; };
struct PSInput { float4 position : SV_POSITION; float2 texCoord : TEXCOORD0; };

PSInput VSMain(VSInput input) {
    float2 pos[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };
    float2 uv[3]  = { float2(0,0), float2(2,0), float2(0,2) };
    PSInput o;
    o.position = float4(pos[input.vertexID], 0, 1);
    o.texCoord = uv[input.vertexID];
    return o;
}

[[vk::combinedImageSampler]][[vk::binding(0, 1)]]
Texture2D u_Texture0 : register(t0);
[[vk::combinedImageSampler]][[vk::binding(0, 1)]]
SamplerState u_Sampler0 : register(s0);

float4 PSMain(PSInput i) : SV_TARGET {
    float2 uv = i.texCoord;
    uv.y = 1.0 - uv.y; // Vulkan Y-flip
    float4 c = u_Texture0.Sample(u_Sampler0, uv);
    float g = dot(c.rgb, float3(0.299, 0.587, 0.114));
    return float4(g, g, g, c.a);
    // return float4(1.0, 1.0, 1.0, 1.0);
}
