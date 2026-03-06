#version 330 core
out vec4 FragColor;

in vec2 v_TexCoord;
in vec3 v_Normal;
in vec3 v_FragPos;

uniform sampler2D u_Texture;
uniform vec3 u_ColorModifier;

// ==========================================
// 新增：光源参数 (由 C++ 引擎层传入)
// ==========================================
uniform vec3 u_LightDir;         // 光线照射的方向
uniform vec3 u_LightColor;       // 光的颜色
uniform float u_AmbientStrength; // 环境光强度 (防止背光面变成纯黑)

void main() {
    // 1. 获取物体原本的颜色 (贴图 * 颜色修饰)
    vec4 texColor = texture(u_Texture, v_TexCoord);
    vec3 objectColor = texColor.rgb * u_ColorModifier;

    // 2. 环境光 (Ambient)：模拟光线在空气中无限反弹的基础亮度
    vec3 ambient = u_AmbientStrength * u_LightColor;

    // 3. 漫反射 (Diffuse)：核心魔法！
    vec3 norm = normalize(v_Normal);
    vec3 lightDir = normalize(-u_LightDir); // 光线方向取反，变成“从物体指向光源”
    
    // 点乘算出夹角：光线垂直打在面上最亮(1.0)，平行或在背面全黑(0.0)
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * u_LightColor;

    // 4. 合并光照结果
    vec3 finalColor = (ambient + diffuse) * objectColor;

    FragColor = vec4(finalColor, 1.0);
}