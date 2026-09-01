// water_composite.frag — full-screen composite of WaterSurface onto Lighting.
// Designed to pair with the GLSL generic_fullscreen.vert (Y-up, v_TexCoord.y=0 at top),
// so it does NOT flip Y (unlike copy.hlsl which pairs with its own Y-down VS).

[[vk::combinedImageSampler]][[vk::binding(0, 1)]]
Texture2D u_Texture0 : register(t0);
[[vk::combinedImageSampler]][[vk::binding(0, 1)]]
SamplerState u_Sampler0 : register(s0);

struct PSInput { float4 position : SV_POSITION; float2 texCoord : TEXCOORD0; };

void main(PSInput i, out float4 outColor : SV_TARGET) {
    outColor = u_Texture0.Sample(u_Sampler0, i.texCoord);
}
