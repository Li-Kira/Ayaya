#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D g_Position;
layout(set = 1, binding = 1) uniform sampler2D g_Normal;
layout(set = 1, binding = 2) uniform sampler2D g_Albedo;
layout(set = 1, binding = 3) uniform sampler2D g_PBR;
layout(set = 1, binding = 4) uniform sampler2D g_CustomData;
layout(set = 1, binding = 5) uniform sampler2D u_ShadowMap;
layout(set = 1, binding = 8) uniform samplerCube u_IrradianceMap;
layout(set = 1, binding = 9) uniform samplerCube u_PrefilteredMap;
layout(set = 1, binding = 10) uniform sampler2D u_BRDFLUT;

layout(set = 0, binding = 0) uniform Camera { mat4 u_ViewProjection; vec3 u_CameraPosition; };
layout(set = 0, binding = 1) uniform LightData { vec4 DirLightDir; vec4 DirLightColor; };

layout(push_constant) uniform PC { mat4 u_LightSpaceMatrix; vec3 u_AmbientColor; float u_Intensity; int u_EnvMapEnabled; } pc;

const float PI = 3.14159265359;
float DistributionGGX(vec3 N, vec3 H, float r) { float a=r*r,a2=a*a; float NdH=max(dot(N,H),0.0); float d=(NdH*NdH*(a2-1.0)+1.0); return a2/(PI*d*d); }
float GeometrySchlickGGX(float NdV, float r) { float k=(r+1.0)*(r+1.0)/8.0; return NdV/(NdV*(1.0-k)+k); }
float GeometrySmith(vec3 N, vec3 V, vec3 L, float r) { return GeometrySchlickGGX(max(dot(N,V),0.0),r)*GeometrySchlickGGX(max(dot(N,L),0.0),r); }
vec3 fresnelSchlick(float c, vec3 F0) { return F0+(1.0-F0)*pow(clamp(1.0-c,0.0,1.0),5.0); }
vec3 fresnelSchlickRoughness(float c, vec3 F0, float r) { return F0+(max(vec3(1.0-r),F0)-F0)*pow(clamp(1.0-c,0.0,1.0),5.0); }

float ShadowCalculation(vec4 fragPosLight, float NdotL) {
    vec3 proj = fragPosLight.xyz/fragPosLight.w;
    proj.x = proj.x*0.5+0.5;
    proj.y = proj.y*(-0.5)+0.5; // Vulkan viewport Y-flip compensation
    if(proj.z>1.0 || proj.x<0.0 || proj.x>1.0 || proj.y<0.0 || proj.y>1.0) return 0.0;
    float bias = max(0.001*(1.0-NdotL), 0.0001);
    float shadow=0.0; vec2 ts=1.0/textureSize(u_ShadowMap,0);
    for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++)
        shadow+=proj.z-bias>texture(u_ShadowMap,proj.xy+vec2(x,y)*ts).r?1.0:0.0;
    return shadow/9.0;
}

void main() {
    vec4 posData = texture(g_Position, v_TexCoord);
    if (posData.a < 0.1) discard;
    vec3 FragPos = posData.rgb;
    vec4 clipPos = u_ViewProjection * vec4(FragPos, 1.0);
    gl_FragDepth = (clipPos.z/clipPos.w)*0.5+0.5;

    vec3 N = normalize(texture(g_Normal, v_TexCoord).rgb);
    vec3 Albedo = texture(g_Albedo, v_TexCoord).rgb;
    vec4 pbr = texture(g_PBR, v_TexCoord);
    float Metallic = pbr.r, Roughness = max(pbr.g, 0.04), AO = pbr.b;
    float ReceiveShadows = texture(g_CustomData, v_TexCoord).r;

    vec3 V = normalize(u_CameraPosition - FragPos);
    vec3 F0 = mix(vec3(0.04), Albedo, Metallic);
    vec3 Lo = vec3(0.0);

    vec3 L = normalize(-DirLightDir.xyz);
    vec3 H = normalize(V+L);
    float NdotL = max(dot(N,L),0.0);
    float D = DistributionGGX(N,H,Roughness), G = GeometrySmith(N,V,L,Roughness);
    vec3 F = fresnelSchlick(max(dot(H,V),0.0),F0);
    vec3 spec = (D*G*F)/max(4.0*max(dot(N,V),0.0)*NdotL,0.001);
    vec3 kS = F; vec3 kD = (1.0-kS)*(1.0-Metallic);

    vec4 fragPosLight = pc.u_LightSpaceMatrix * vec4(FragPos, 1.0);
    float shadow = ShadowCalculation(fragPosLight, NdotL) * ReceiveShadows;
    Lo += (kD*Albedo/PI+spec)*DirLightColor.rgb*NdotL*(1.0-shadow);

    float NdV = max(dot(N,V),0.0);
    vec3 F_ibl = fresnelSchlickRoughness(NdV,F0,Roughness);
    vec3 kS_ibl = F_ibl, kD_ibl = (1.0-kS_ibl)*(1.0-Metallic);
    vec3 irradiance = pc.u_AmbientColor;
    vec3 spec_ibl = vec3(0.0);
    if (pc.u_EnvMapEnabled == 1) {
        irradiance += texture(u_IrradianceMap, N).rgb * pc.u_Intensity;
        vec3 R = reflect(-V, N);
        vec3 prefiltered = textureLod(u_PrefilteredMap, R, Roughness*4.0).rgb * pc.u_Intensity;
        vec2 brdf = texture(u_BRDFLUT, vec2(max(NdV,1e-5), Roughness)).rg;
        spec_ibl = prefiltered * (F_ibl * brdf.x + brdf.y);
    }
    vec3 ambient = (kD_ibl*irradiance*Albedo + spec_ibl) * AO;
    FragColor = vec4(ambient + Lo, 1.0);
}
