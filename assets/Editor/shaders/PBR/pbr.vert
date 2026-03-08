#version 410 core

layout (location = 0) in vec3 a_Position;
layout (location = 1) in vec3 a_Normal;
layout (location = 2) in vec2 a_TexCoord;
layout (location = 3) in vec3 a_Tangent;

out vec3 v_FragPos;
out vec2 v_TexCoord;
out mat3 v_TBN; // <--- 传给片段着色器的 TBN 矩阵

uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;

void main() {
    v_FragPos = vec3(u_Transform * vec4(a_Position, 1.0));
    v_TexCoord = a_TexCoord;

    // 1. 将法线和切线转换到世界空间
    // (注意：严格来说应使用法线矩阵 transpose(inverse(mat3(u_Transform)))，如果只有等比缩放可以直接转 mat3)
    mat3 normalMatrix = transpose(inverse(mat3(u_Transform)));
    vec3 T = normalize(normalMatrix * a_Tangent);
    vec3 N = normalize(normalMatrix * a_Normal);

    // 2. 施加格拉姆-施密特正交化 (Gram-Schmidt process)
    // 防止因为模型平滑导致的切线和法线不再绝对垂直
    T = normalize(T - dot(T, N) * N);

    // 3. 计算副切线 B
    vec3 B = cross(N, T);

    // 4. 组装 TBN 矩阵
    v_TBN = mat3(T, B, N);

    gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
}