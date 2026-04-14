#version 450 core

// ==========================================
// 1. 接收顶点着色器传来的数据
// ==========================================
layout(location = 0) in vec3 v_FragPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

// ==========================================
// 2. 颜色输出
// ==========================================
// 对应 m_ForwardFBO 的第一个颜色附件
layout(location = 0) out vec4 o_Color; 

// ==========================================
// 3. 材质贴图
// ==========================================
// Set 1: 对应你 VulkanPipeline 中的 Texture 描述符集
// Binding 0: 假设你的 Material 默认将基础颜色贴图绑定在槽位 0
layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;

void main() {
    // 1. 采样材质颜色
    // 如果没有指定贴图，你的引擎会绑定默认的 WhiteTexture，所以这里出来的会是纯白 vec4(1.0)
    vec4 albedo = texture(u_AlbedoMap, v_TexCoord);

    // 2. 法线归一化 (经过插值后的法线长度可能不为 1)
    vec3 normal = normalize(v_Normal);

    // 3. 极简的光照测试 (写死一束从右上方打过来的平行光)
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

    // 计算漫反射 (N dot L)，如果背光则为 0
    float diff = max(dot(normal, lightDir), 0.0);

    // 给一个微弱的环境光底色，防止背光面死黑一片
    vec3 ambient = vec3(0.15) * albedo.rgb;
    vec3 diffuse = diff * albedo.rgb;

    // 4. 混合最终颜色
    vec3 finalColor = ambient + diffuse;

    // 输出最终颜色和透明度
    o_Color = vec4(finalColor, albedo.a);
}