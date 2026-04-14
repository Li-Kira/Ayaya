#version 450 core

// ==========================================
// 1. 顶点输入 (请与你的 BufferLayout 顺序保持一致)
// ==========================================
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
// 如果你的引擎顶点结构体里还有 Tangent/Bitangent，可以继续往下写 location = 3, 4

// ==========================================
// 2. 传递给片段着色器的变量 (Varyings)
// ==========================================
layout(location = 0) out vec3 v_FragPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;

// ==========================================
// 3. 全局数据与推送常量
// ==========================================
// Set 0, Binding 0: 对应你的 s_GlobalUBOs 和 Camera UBO
layout(set = 0, binding = 0) uniform CameraData {
    mat4 ViewProjection;
} u_Camera;

// Push Constant: 模型变换矩阵 (最大 128 或 256 字节)
layout(push_constant) uniform TransformData {
    mat4 ModelMatrix;
} u_Push;

void main() {
    // 1. 计算世界空间坐标
    vec4 worldPosition = u_Push.ModelMatrix * vec4(a_Position, 1.0);
    v_FragPos = worldPosition.xyz;

    // 2. 转换法线到世界空间
    // (注意: 如果你的模型有非等比缩放，这里应该用 ModelMatrix 的逆转置矩阵。
    // 为了基础测试，我们先直接使用 ModelMatrix 提取旋转部分)
    v_Normal = mat3(u_Push.ModelMatrix) * a_Normal;

    // 3. 传递 UV
    v_TexCoord = a_TexCoord;

    // 4. 计算最终的裁剪空间坐标
    // ⚠️ 记得你在 CommandBuffer 里开启了负高度视口 (Y轴反转)，所以这里不需要写 gl_Position.y = -gl_Position.y; 
    gl_Position = u_Camera.ViewProjection * worldPosition;
}