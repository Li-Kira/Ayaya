#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent; // 保证 44 字节顶点步长

// 与 C++ 的 struct_CameraData 严丝合缝
layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection; // 64 bytes
    vec3 CameraPosition; // 12 bytes
} u_Camera;

layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
    vec3 Albedo;
    int UseAlbedoMap;
    float Metallic;
    float Roughness;
    float AO;
    int UseMetallicMap;
    int UseRoughnessMap;
    int UseAOMap;
    int UseNormalMap;
} u_Push;

// ==========================================
// 【核心修复】：必须与 pbr_forward.frag 的输入槽位(location)和类型绝对一致！
// ==========================================
layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;

void main() {
    // 1. 计算世界坐标
    vec4 worldPos = u_Push.ModelMatrix * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    
    // 2. 计算世界法线 (为了性能这里直接用 mat3，如果模型有极端的非等比缩放，可以使用逆转置矩阵)
    v_Normal = mat3(u_Push.ModelMatrix) * a_Normal;
    
    // 3. 传递 UV
    v_TexCoord = a_TexCoord;
    
    // 4. 最终裁剪空间坐标
    gl_Position = u_Camera.ViewProjection * worldPos;
}