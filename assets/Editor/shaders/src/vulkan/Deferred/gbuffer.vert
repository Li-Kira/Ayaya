#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

layout(set = 0, binding = 0) uniform Camera {
    mat4 u_ViewProjection;
    vec3 u_CameraPosition;
};

layout(push_constant) uniform Constants {
    mat4 u_Transform;         
    vec3 u_Albedo;            
    float u_ReceiveShadows;   
    float u_Metallic;         
    float u_Roughness;        
    float u_AO;               
    int u_UseAlbedoMap;       
    int u_UseMetallicMap;     
    int u_UseRoughnessMap;    
    int u_UseAOMap;           
    int u_UseNormalMap;       
} pc;

layout(location = 0) out vec3 v_FragPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;

void main() {
    // 【修复】：加上 pc. 前缀
    vec4 worldPos = pc.u_Transform * vec4(a_Position, 1.0);
    v_FragPos = worldPos.xyz;
    
    mat3 normalMatrix = transpose(inverse(mat3(pc.u_Transform)));
    v_Normal = normalMatrix * a_Normal;
    
    v_TexCoord = a_TexCoord;
    gl_Position = u_ViewProjection * worldPos;
}