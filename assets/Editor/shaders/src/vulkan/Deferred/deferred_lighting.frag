#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 v_TexCoord;

layout(set = 1, binding = 0) uniform sampler2D u_DepthMap;
layout(set = 1, binding = 1) uniform sampler2D g_Albedo;
layout(set = 1, binding = 2) uniform sampler2D g_PBR;
layout(set = 1, binding = 3) uniform sampler2D g_CustomData;
layout(set = 1, binding = 4) uniform sampler2D g_Normal;
layout(set = 1, binding = 5) uniform sampler2D u_ShadowMap;
layout(set = 1, binding = 8) uniform samplerCube u_IrradianceMap;
layout(set = 1, binding = 9) uniform samplerCube u_PrefilteredMap;
layout(set = 1, binding = 10) uniform sampler2D u_BRDFLUT;
layout(set = 1, binding = 11) uniform sampler2D u_SSAO;

layout(set = 0, binding = 0) uniform Camera { mat4 u_ViewProjection; mat4 u_View; vec3 u_CameraPosition; };

struct PointLight { vec4 Position; vec4 Color; };
layout(set = 0, binding = 1) uniform LightData { vec4 DirLightDir; vec4 DirLightColor; PointLight PointLights[4]; int PointLightCount; };

layout(push_constant) uniform PC {
    mat4 u_LightSpaceMatrix;
    vec3 u_AmbientColor; float u_Intensity;
    int u_EnvMapEnabled; int u_EnableSSAO;
    mat4 u_InverseViewProj;
} pc;

// ─── Debug mode: 0=normal, 1=depth, 2=normal, 3=albedo, 4=PBR, 5=worldPos, 6=FragPos+dpeth, 7=ambientOnly, 8=directOnly, 9=iblOnly ───
#define DEBUG_MODE 0

// Octahedral decode: 2-component [0,1] → unit vec3
vec3 OctDecode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

const float PI = 3.14159265359;
float D_GGX(vec3 N, vec3 H, float r){float a=r*r,a2=a*a;float NdH=max(dot(N,H),0.0);float d=NdH*NdH*(a2-1.0)+1.0;return a2/(PI*d*d);}
float G_Schlick(float NdV,float r){float k=(r+1.0)*(r+1.0)/8.0;return NdV/(NdV*(1.0-k)+k);}
float G_Smith(vec3 N,vec3 V,vec3 L,float r){return G_Schlick(max(dot(N,V),0.0),r)*G_Schlick(max(dot(N,L),0.0),r);}
vec3 F_Schlick(float c,vec3 F0){return F0+(1.0-F0)*pow(clamp(1.0-c,0.0,1.0),5.0);}
vec3 F_SchlickR(float c,vec3 F0,float r){return F0+(max(vec3(1.0-r),F0)-F0)*pow(clamp(1.0-c,0.0,1.0),5.0);}

float ShadowCalc(vec4 fl, float NdotL) {
    vec3 p = fl.xyz/fl.w;
    p.x = p.x * 0.5 + 0.5;  p.y = p.y * (-0.5) + 0.5;  // Vulkan viewport Y-flip compensation
    if(p.z>1.0||p.x<0.0||p.x>1.0||p.y<0.0||p.y>1.0) return 0.0;
    float bias=max(0.001*(1.0-NdotL),0.0001);
    float s=0.0; vec2 ts=1.0/textureSize(u_ShadowMap,0);
    for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++)
        s+=p.z-bias>texture(u_ShadowMap,p.xy+vec2(x,y)*ts).r?1.0:0.0;
    return s/9.0;
}

void main() {
    float depth = texture(u_DepthMap, v_TexCoord).r;
    if (depth >= 1.0) discard;

    vec4 ndc = vec4(v_TexCoord.x * 2.0 - 1.0, 1.0 - v_TexCoord.y * 2.0, depth, 1.0);
    vec4 wp = pc.u_InverseViewProj * ndc;
    vec3 FragPos = wp.xyz / wp.w;
    vec4 cp = u_ViewProjection * vec4(FragPos, 1.0);
    gl_FragDepth = (cp.z / cp.w) * 0.5 + 0.5;

    vec3 N = OctDecode(texture(g_Normal, v_TexCoord).rg);
    vec3 Albedo = texture(g_Albedo, v_TexCoord).rgb;
    vec4 pbr = texture(g_PBR, v_TexCoord);
    float Metallic = pbr.r, Roughness = max(pbr.g, 0.04), AO = pbr.b;
    float RcvShadow = texture(g_CustomData, v_TexCoord).r;

    vec3 V = normalize(u_CameraPosition - FragPos);
    vec3 F0 = mix(vec3(0.04), Albedo, Metallic);

#if DEBUG_MODE == 1
    // Depth map (hw depth from GBuffer)
    FragColor = vec4(depth, depth, depth, 1.0); return;
#elif DEBUG_MODE == 2
    // World-space normals
    FragColor = vec4(N * 0.5 + 0.5, 1.0); return;
#elif DEBUG_MODE == 3
    // Albedo
    FragColor = vec4(Albedo, 1.0); return;
#elif DEBUG_MODE == 4
    // PBR: R=Metallic, G=Roughness, B=AO
    FragColor = vec4(Metallic, Roughness, AO, 1.0); return;
#elif DEBUG_MODE == 5
    // World position as color
    FragColor = vec4(fract(FragPos * 0.5), 1.0); return;
#elif DEBUG_MODE == 6
    // Compare: R=hwDepth, G=CustomData.b (gl_FragCoord.z from GBuffer)
    float gbZ = texture(g_CustomData, v_TexCoord).b;
    FragColor = vec4(depth, gbZ, 0.0, 1.0); return;
#endif

    // =================================================================
    //  PBR Lighting
    // =================================================================
    vec3 Lo = vec3(0.0);

    // Directional Light
    vec3 L = normalize(-DirLightDir.xyz);
    vec3 H = normalize(V+L);
    float NdotL = max(dot(N,L),0.0);
    float D = D_GGX(N,H,Roughness), G = G_Smith(N,V,L,Roughness);
    vec3 F = F_Schlick(max(dot(H,V),0.0),F0);
    vec3 spec = (D*G*F)/max(4.0*max(dot(N,V),0.0)*NdotL,0.001);
    vec3 kS=F,kD=(1.0-kS)*(1.0-Metallic);
    float shadow = ShadowCalc(pc.u_LightSpaceMatrix*vec4(FragPos,1.0), NdotL) * RcvShadow;
    Lo += (kD*Albedo/PI+spec)*DirLightColor.rgb*NdotL*(1.0-shadow);

    // Point Lights — distance-culled per pixel
    for(int i=0;i<PointLightCount&&i<4;i++){
        vec3 lp=PointLights[i].Position.xyz; float lr=PointLights[i].Position.w;
        // Early distance cull: skip expensive PBR math if pixel is outside light radius
        vec3 toLight = lp - FragPos;
        float d2 = dot(toLight, toLight);
        if (d2 > lr * lr) continue;
        float d = sqrt(d2);

        vec3 lc=PointLights[i].Color.rgb; float lf=PointLights[i].Color.w;
        vec3 Lp=toLight / d; vec3 Hp=normalize(V+Lp);
        float att=1.0/(d*d+0.0001);
        float dbr=d/lr, dbr4=pow(dbr,4.0);
        float w=clamp(1.0-dbr4,0.0,1.0); w=pow(w,lf+1.0); att*=w;
        vec3 rad=lc*att; float NdotLp=max(dot(N,Lp),0.0);
        if (NdotLp <= 0.0) continue;
        float Dp=D_GGX(N,Hp,Roughness), Gp=G_Smith(N,V,Lp,Roughness);
        vec3 Fp=F_Schlick(max(dot(Hp,V),0.0),F0);
        vec3 sp=(Dp*Gp*Fp)/max(4.0*max(dot(N,V),0.0)*NdotLp,0.001);
        vec3 kSp=Fp,kDp=(1.0-kSp)*(1.0-Metallic);
        Lo+=(kDp*Albedo/PI+sp)*rad*NdotLp;
    }

    // IBL
    float NdV=max(dot(N,V),0.0);
    vec3 F_ibl=F_SchlickR(NdV,F0,Roughness);
    vec3 kSi=F_ibl,kDi=(1.0-kSi)*(1.0-Metallic);
    vec3 irr=pc.u_AmbientColor; vec3 spI=vec3(0.0);
    if(pc.u_EnvMapEnabled==1){
        irr+=texture(u_IrradianceMap,N).rgb*pc.u_Intensity;
        vec3 R=reflect(-V,N);
        vec3 pf=textureLod(u_PrefilteredMap,R,Roughness*4.0).rgb*pc.u_Intensity;
        vec2 brdf=texture(u_BRDFLUT,vec2(max(NdV,1e-5),Roughness)).rg;
        spI=pf*(F_ibl*brdf.x+brdf.y);
    }
    float ssao=(pc.u_EnableSSAO==1)?texture(u_SSAO,v_TexCoord).r:1.0;
    vec3 amb=(kDi*irr*Albedo+spI)*AO*ssao;

#if DEBUG_MODE == 7
    FragColor = vec4(amb, 1.0); return;
#elif DEBUG_MODE == 8
    FragColor = vec4(Lo, 1.0); return;
#elif DEBUG_MODE == 9
    FragColor = vec4(spI, 1.0); return;
#endif

    FragColor = vec4(amb + Lo, 1.0);
}
