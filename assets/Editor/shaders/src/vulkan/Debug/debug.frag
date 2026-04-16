#version 450 core

layout(location = 0) out vec4 o_Color;
layout(location = 0) in vec2 v_TexCoord;

// 接收我们在 C++ 中绑定的 Set 1 (贴图槽位)
layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;

// 接收 C++ 传过来的 Push Constants
layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
    vec3 Albedo;
    int UseAlbedoMap;
} u_Push;

void main() {
    // 默认基础颜色为白色 (1.0, 1.0, 1.0, 1.0)
    vec4 texColor = vec4(1.0);

    // 如果 C++ 告诉我们这个材质有贴图 (UseAlbedoMap == 1)，就去采样贴图
    if (u_Push.UseAlbedoMap == 1) {
        texColor = texture(u_AlbedoMap, v_TexCoord);
    }

    // 最终颜色 = 贴图颜色 * 材质基础颜色 (Albedo)
    // 如果没有贴图，texColor 就是纯白，结果就是纯粹的 Albedo 颜色
    o_Color = vec4(texColor.rgb * u_Push.Albedo, 1.0);
}