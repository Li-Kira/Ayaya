# Ayaya Engine — Vulkan 渲染架构与管线深度分析报告

> **初始报告:** 2026-06-18 | **更新:** 2026-06-20 (GDR CPU 剔除消除 + Transform 缓存 + VSync 接入 + Alpha Map 管线 + Lua 性能脚本)
> **分析范围:** `/src/Engine/Platform/Vulkan/` (28 文件), `/src/Engine/Renderer/` (抽象层 + 12 个 Pass), `/assets/Editor/shaders/src/vulkan/` (52 个 Shader)

---

## 目录

1. [架构全景](#1-架构全景)
2. [基础层: VulkanContext & 同步机制](#2-基础层-vulkancontext--同步机制)
3. [RHI 抽象层: RenderCommandBuffer & Pipeline](#3-rhi-抽象层-rendercommandbuffer--pipeline)
4. [资源管理](#4-资源管理)
5. [RenderGraph DAG 帧图系统](#5-rendergraph-dag-帧图系统)
6. [延迟渲染管线逐 Pass 分析](#6-延迟渲染管线逐-pass-分析)
7. [Shader 架构](#7-shader-架构)
8. [材质与资产系统](#8-材质与资产系统)
9. [深度优化机会评估](#9-深度优化机会评估)
10. [GDR 性能优化](#18-gdr-性能优化-2026-06-20)

---

## 1. 架构全景

### 1.1 整体分层

```
┌─────────────────────────────────────────────────────┐
│  EditorLayer / SceneRenderer (编排层)                │
├─────────────────────────────────────────────────────┤
│  RenderGraph (DAG 帧图)         RenderQueue (排序)   │
├─────────────────────────────────────────────────────┤
│  12 个 RenderPass (Shadow→UI)                       │
├─────────────────────────────────────────────────────┤
│  RenderCommandBuffer (RHI 抽象)                      │
├─────────────────────────────────────────────────────┤
│  VulkanContext / VMA / BindlessManager              │
├─────────────────────────────────────────────────────┤
│  Vulkan 1.3 Driver (Dynamic Rendering)              │
└─────────────────────────────────────────────────────┘
```

### 1.2 核心设计原则

| 原则 | 实现方式 |
|------|---------|
| **双后端支持** | 静态工厂模式 `Create()` → `switch(RendererAPI::GetAPI())` → OpenGL or Vulkan |
| **3 帧飞行** | 所有 CPU 写入资源 (UBO/SSBO/DescriptorSet/FBO) 均三重缓冲 |
| **动态渲染 (Vulkan 1.3)** | 零 `VkRenderPass`/`VkFramebuffer`，全部使用 `vkCmdBeginRendering` |
| **VMA 内存管理** | 所有 GPU 分配经 VMA，HOST_COHERENT 消除显式 flush |
| **延迟渲染** | GBuffer (4 MRT + Depth) → Lighting → Forward Overlay → WBOIT → Post |
| **PBR + IBL** | Cook-Torrance GGX 微面元 + Split-Sum 近似环境光照 |

### 1.3 关键数字

| 指标 | 数值 |
|------|------|
| Frames in Flight | 3 |
| GBuffer 附件数 | 5 (RG16F + RGBA8×3 + Depth) |
| 最大点光源数 | 4 (Deferred) / 8 (WBOIT) |
| Push Constants 上限 | 256 bytes |
| 纹理描述符集 Ring Buffer | 1000 个/帧 (~10x 余量) |
| Bindless 纹理容量 | min(perStageSamplers, perSetSamplers, 100000) |
| GPU Timestamp 查询数 | 96 (16 passes × 2 slots × 3 frames) |
| WBOIT 最大实例数 | 2048 |
| GBuffer 最大实例数 | 4096 |
| UI 批次最大 Quad 数 | 10000 (16 纹理槽位) |

---

## 2. 基础层: VulkanContext & 同步机制

### 2.1 初始化流程

```
VulkanContext::Init()
  ├─ 创建 VkInstance (VK_KHR_portability_enumeration on macOS)
  ├─ 创建 VkSurfaceKHR
  ├─ 选取物理设备 (discrete > integrated)
  ├─ 查询 Descriptor Indexing 能力
  ├─ 创建 VkDevice (Vulkan 1.3 + dynamicRendering feature)
  ├─ 创建 VMA Allocator (VK_API_VERSION_1_3)
  ├─ 创建全局 Descriptor Pool (1000 per type, FREE_DESCRIPTOR_SET_BIT)
  ├─ 创建 BindlessManager
  ├─ 创建 GeometryPool
  ├─ 创建 Swapchain + 3 帧同步对象
  └─ 创建 Timestamp Query Pool (96 queries)
```

### 2.2 每帧循环

```
BeginFrame()
  ├─ vkWaitForFences(inFlightFences[currentFrame])     ← CPU 等待 GPU 完成本槽位
  ├─ vkAcquireNextImageKHR()                             ← 获取交换链图像
  ├─ vkResetFences(inFlightFences[currentFrame])
  ├─ vkResetCommandBuffer(commandBuffers[currentFrame])
  ├─ 重置所有 Pipeline 的纹理描述符环索引
  ├─ vkResetQueryPool(timestampPool, ...)
  └─ vkBeginCommandBuffer()

  ... 整个渲染管线执行 ...

SwapBuffers()
  ├─ vkEndCommandBuffer()
  ├─ vkQueueSubmit()  ← 信号量: wait(imageAvailable) → signal(renderFinished)
  │                     围栏: signal(inFlightFences[currentFrame])
  ├─ vkQueuePresentKHR() ← wait(renderFinished)
  └─ m_CurrentFrame = (m_CurrentFrame + 1) % 3
```

### 2.3 同步机制全景

| 机制 | 用途 | 数量 |
|------|------|------|
| **Fence** ( signaled ) | CPU-GPU 帧边界同步 | 3 (每帧 1 个) |
| **Binary Semaphore** | Swapchain acquire → submit → present | 2×3 (imageAvailable + renderFinished) |
| **Pipeline Barrier** | Attachment write → shader read | 每 Pass 边界 (RenderGraph 自动) |
| **Memory Barrier** | 全局执行屏障 | `InsertExecutionBarrier()` |
| **Image Barrier** | 精确布局转换 | `TransitionImageLayout()` |

### 2.4 单次命令提交

```cpp
// VulkanContext::EndSingleTimeCommands()
// 无 Fence → vkQueueWaitIdle + vkDeviceWaitIdle + vkResetCommandPool
// ⚠️ 这是一个全 GPU 阻塞操作，用于纹理上传/IBL 烘焙等一次性任务
```

**优化点 #1:** `EndSingleTimeCommands()` 的 `vkQueueWaitIdle` + `vkDeviceWaitIdle` 双重等待在纹理批量上传时会严重阻塞。可改用 per-transfer Fence + 批量提交模式。

### 2.5 GPU Timestamp

```cpp
// 线性分配器：每帧重置
AllocTimestampSlot() → 返回 2 个连续 query index (start + end)
ReadTimestampResults() → 读取 N-1 帧的结果
  // ⚠️ 关键修复：预清零 availability bits 防止 VK_NOT_READY 时读到旧数据
```

---

## 3. RHI 抽象层: RenderCommandBuffer & Pipeline

### 3.1 VulkanRenderCommandBuffer 架构

这是整个引擎最关键的 RHI 实现。核心设计：**延迟描述符批量写入**。

```
帧内调用序列:
  BeginRenderPass()         → 构建 VkRenderingInfo，begin dynamic rendering
  BindPipeline(pipeline)    → 绑定 PSO + Set 0 (Camera UBO) + 可选 Bindless Set
                                清空 m_PendingImageInfos (关键！)
  PushConstantData(...)     → vkCmdPushConstants (Vertex|Fragment, 0 offset)
  BindTexture2D(slot, tex)  → 仅写入 m_PendingImageInfos map，不做 GPU 调用
  BindTextureCube(...)      → 同上
  DrawIndexed(mesh)         → FlushDescriptorSets() → vkCmdBindDescriptorSets → draw
  EndRenderPass()           → vkCmdEndRendering
```

### 3.2 延迟描述符批量写入 (关键设计)

```
m_PendingImageInfos: map<slot, VkDescriptorImageInfo>

BindTexture2D(slot=1, texA)  → m_PendingImageInfos[1] = {viewA, sampler, layout}
BindTexture2D(slot=2, texB)  → m_PendingImageInfos[2] = {viewB, sampler, layout}
// 同名同 slot 同 imageView → 跳过 (去重优化)
DrawIndexed(mesh) →
  FlushDescriptorSets():
    set = pipeline->GetNextTextureDescriptorSet()  // 环缓冲区取预分配 set
    vkUpdateDescriptorSets(set, writes from m_PendingImageInfos)  // 一次性批量写入
    vkCmdBindDescriptorSets(set=1, set)
    // ⚠️ 不清理 m_PendingImageInfos —— 后续 draw 可能只绑定部分纹理，需要保留之前的
```

**⚠️ 关键 Bug 修复记录:** `FlushDescriptorSets()` 之前清理了 `m_PendingImageInfos`，导致后续 draw (如 IBL 环境贴图) 丢失之前绑定的纹理。现在仅 `BindPipeline()` 清理（因为不同 PSO 的 descriptor layout 不同）。

### 3.3 VulkanPipeline 描述符集成架构

```
三层描述符体系:

Set 0 — 全局 UBO (Camera + LightData)
  ├─ 3 帧独立的 VkDescriptorSet
  ├─ 通过静态 s_GlobalUBOs 全局注册
  └─ RefreshDescriptorSets() 重新写入 UBO 变化

Set 1 — 纹理 (Per-Material Textures)
  ├─ 每个 Pipeline 有 3×1000 预分配 descriptor set 环缓冲区
  ├─ GetNextTextureDescriptorSet() 旋转分配
  └─ BeginFrame() 时 ResetTextureDescriptorIndex() 重置

Set 2 — 实例化 SSBO (可选)
  ├─ 通过 s_ExtraSetLayouts 在 Create() 前注入
  └─ GBuffer/WBOIT 的实例化路径使用

Bindless — 全局纹理数组 (替代 Set 1)
  └─ UseBindlessTextures=true 时，Set 1 = 全局 bindless layout
```

### 3.4 Push Constants 模型

```cpp
// 256 bytes 上限，Vertex | Fragment 阶段可见
// 每个 Pass 有自己的 Push Constant 结构体，alignas(16) 对齐

// 典型大小:
GBuffer:      ~112 bytes (Transform + PBR params + texture flags)
Lighting:     ~160 bytes (LightSpaceMatrix + Ambient + InverseViewProj)
SSAO:         ~80 bytes
WBOIT Gather: ~96 bytes
PostProcess:  ~32 bytes
UI:           64 bytes (mat4 ortho projection)
```

**优化点 #2:** 当前 Push Constants 上限 256 bytes 足够但边界紧张。某些 Pass (如 ForwardTest) 的 Push Constants 已经接近上限。如果需要为未来功能扩展（如 clustered lighting 的 light grid），需要考虑提升到 `maxPushConstantsSize` (通常 128-256 bytes，取决于硬件)。

---

## 4. 资源管理

### 4.1 VulkanTexture2D

```
创建流程:
  stbi_load (CPU) → 创建 VMA staging buffer → memcpy
  → vkCmdPipelineBarrier(UNDEFINED → TRANSFER_DST)
  → vkCmdCopyBufferToImage
  → vkCmdPipelineBarrier(TRANSFER_DST → SHADER_READ_ONLY)
  → 提交 + 等待 → 销毁 staging buffer
  → 注册到 BindlessManager
```

**关键特征:**
- 始终 mipLevels = 1 (无 Mipmap)
- Anisotropy 禁用
- Staging buffer 即用即毁
- Bindless 索引通过 `AllocateIndex()` 分配，`FreeIndex()` 回收 (free-list)

**优化点 #3:** 无 Mipmap 生成——所有纹理以最高分辨率采样。对于远处物体和 IBL prefiltered map，缺少 mipmap 导致:
- 纹理采样 Cache 效率低
- 远处物体出现锯齿/摩尔纹
- 带宽浪费

### 4.2 VulkanFramebuffer

```
使用 Vulkan 1.3 Dynamic Rendering → 无 VkFramebuffer 对象

Invalidate():
  ├─ 为每个 color attachment 创建 VkImage + VkImageView
  ├─ 为 depth attachment 创建 VkImage + VkImageView
  ├─ VMA 分配 (VMA_MEMORY_USAGE_AUTO)
  ├─ 一次性 barrier: UNDEFINED → SHADER_READ_ONLY (color) / DEPTH_STENCIL_ATTACHMENT (depth)
  └─ 注册 ImGui descriptor set (用于编辑器面板预览)

使用标志:
  Color: COLOR_ATTACHMENT | SAMPLED | TRANSFER_SRC | TRANSFER_DST
  Depth: DEPTH_STENCIL_ATTACHMENT | SAMPLED (fallback: 仅 ATTACHMENT)
```

**⚠️ 已知问题:** VulkanFramebuffer 的 depth attachment 在部分平台不支持 `SAMPLED_BIT` — 代码中有 fallback 逻辑，创建失败后重试不带 `SAMPLED_BIT`。

### 4.3 VulkanUniformBuffer

```
三帧独立 VkBuffer + VmaAllocation:
  m_Buffers[3], m_Allocations[3]
  每个: VMA_ALLOCATION_CREATE_MAPPED_BIT + HOST_COHERENT

SetData(data, size):
  memcpy(m_MappedPointers[currentFrame], data, size)
  // 无 vkFlushMappedMemoryRanges (HOST_COHERENT 保证)

SetDataAllFrames(data, size):
  // 同时写入全部 3 帧 (用于一次性提交，如 AssetPreviewer)
```

**⚠️ 关键修复:** `VK_MEMORY_PROPERTY_HOST_COHERENT_BIT` 是 `allocInfo.requiredFlags` — 解决了 "occasional flickering, data not synced to VRAM in time" 的问题。

### 4.4 VulkanStorageBuffer

```
与 UniformBuffer 相同的三帧 persistent-mapped 模式
用途: GBuffer 实例化 Transform 数组 (SSBO, Set=2, Binding=0)
      WBOIT 实例化 Transform 数组 (SSBO, Set=2, Binding=0)

kMaxInstances: GBuffer = 4096, WBOIT = 2048
```

### 4.5 VulkanBindlessManager

```
设计: 单一全局 descriptor set，binding=0 为 COMBINED_IMAGE_SAMPLER 数组
容量: min(perStageSamplers, perSetSamplers, 100000)

特性:
  - UPDATE_AFTER_BIND + PARTIALLY_BOUND + VARIABLE_DESCRIPTOR_COUNT
  - Index 0 保留 (表示 "未注册")
  - Free-list 回收 (LIFO)
  - 每个纹理上传后调用 UpdateBinding() 写入数组元素

使用场景:
  - UI Pass 的 bindless 纹理数组
  - GBuffer GDR (GPU-Driven) 路径的 bindless material textures (计划中)
```

**优化点 #4:** Bindless 目前仅 UI Pass 使用。整个延迟渲染管线仍然使用传统 Set=1 纹理绑定方式——GBuffer/Lighting/WBOIT 每个 material 切换都需 `vkUpdateDescriptorSets`。将 material textures 全部迁移到 bindless 可大幅减少 descriptor set 写入。

---

## 5. RenderGraph DAG 帧图系统

### 5.1 核心类型

```
RGTexture:    具名渲染目标 → 3 个物理 FBO (三重缓冲)
              CurrentLayout[3] / DepthLayout[3] (逐帧布局追踪)

RGBuilder:    DSL — ReadTexture / WriteTexture / ReadWriteTexture

RGPass:       节点 — TextureReads + TextureWrites + ExecuteCallback

RenderGraph:  DAG 引擎 — Compile (拓扑排序) → Execute (布局状态机)
```

### 5.2 Compile 阶段

```
1. 构建 Producer Map (每个纹理一个生产者)
2. ReadWriteTexture → 隐式边: 当前写入者 depends on 前一个写入者
3. Kahn 拓扑排序
4. 环检测 (不可达 Pass 追加到尾部 + 错误日志)
5. 为每个 RGTexture 创建 3 个物理 FBO
   初始布局: ShaderReadOnlyOptimal (color) / DepthStencilAttachmentOptimal (depth)
```

### 5.3 Execute 阶段 — 自动屏障状态机

```
For each Pass in topological order:
  1. EnsureReadable(readTextures):
     if layout != ShaderReadOnly → TransitionImageLayout → ShaderReadOnly
  2. EnsureWritable(writeTextures):
     if layout != ColorAttachment/DepthStencilAttachment → TransitionImageLayout → Attachment
  3. 注入 currentFrame FBO 到 context.Framebuffers
  4. 执行 ExecuteCallback
  5. InsertTileResolveBarrier(writeTextures):
     Color: COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
            srcStage=COLOR_ATTACHMENT_OUTPUT → dstStage=FRAGMENT_SHADER
     Depth: DEPTH_STENCIL_ATTACHMENT_OPTIMAL → DEPTH_STENCIL_READ_ONLY_OPTIMAL (仅 depth-only textures)
            srcStage=LATE_FRAGMENT_TESTS → dstStage=FRAGMENT_SHADER
```

### 5.4 TBDR 关键处理 (Apple Silicon)

```
问题: Vulkan 1.3 Dynamic Rendering 的 vkCmdEndRendering 不执行隐式布局转换
      (不同于 VkRenderPass 的 finalLayout)
      → Tile-based GPU 的 on-chip tile 数据不会自动 flush 到系统内存

解决: InsertTileResolveBarrier() 在每个 Write Pass 后插入
      VkImageMemoryBarrier 确保 tile cache → system memory

⚠️ 已知问题 (来自 CLAUDE.md):
  "InsertTileResolveBarrier 和 EnsureWritable 仅对 depth-only textures 转换 depth attachment。
   对于 color+depth 纹理 (如 'Lighting' RGBA16F+Depth)，depth 在 BeginRenderPass 后保持
   DEPTH_STENCIL_ATTACHMENT_OPTIMAL 且不会再转换回 read-only。
   后续 Pass 读取此类纹理的 depth 可能读到过期数据。"
```

**优化点 #5:** RenderGraph 的 `CurrentLayout` 仅追踪单一布局，但 color+depth 纹理的 color 和 depth 可能有不同布局。这会导致:
- 下游 Pass 读取 depth 时缺少必要的 `DEPTH_STENCIL_ATTACHMENT → DEPTH_STENCIL_READ_ONLY` 转换
- TBDR tile cache 中的 depth 数据可能未 flush

### 5.5 Pass Culling

`RGPass::IsCulled` 字段已预留但**未实现**。所有 Pass 总是执行。

**优化点 #6:** 实现 Pass culling——如果 Pass 的输入纹理无人写入 (如 SSAO 禁用、WBOIT 无半透明物体)，直接跳过整个 Pass + 其屏障。

---

## 6. 延迟渲染管线逐 Pass 分析

### 6.1 管线拓扑总览

```
RenderGraph 执行顺序 (12 Passes):
                                  写入目标 (分辨率)
Stage 1─────────────────────────────────────────────
  ShadowPass          [ShadowMap]       2048×2048 Depth
  GBufferPass         [GBuffer]         VP×VP 5-attach
Stage 1.5───────────────────────────────────────────
  SSAOPass            [SSAO_Final]      VP/2 × VP/2 R8
Stage 2─────────────────────────────────────────────
  LightingPass        [Lighting]        VP×VP RGBA16F+Depth
Stage 3─────────────────────────────────────────────
  ForwardBlendPass    [Lighting] LOAD   Skybox+Grid+Sprite
Stage 3.1-3.2───────────────────────────────────────
  WBOITGatherPass     [WBOIT_Gather]    VP×VP RGBA16F+RG16F
  WBOITResolvePass    [Lighting] LOAD   合成半透明
Stage 3.5───────────────────────────────────────────
  OutlinePass         [Selection]       VP×VP RGBA8
Stage 4─────────────────────────────────────────────
  BloomPass           [Bloom]           VP/2 × VP/2 RGBA16F
  PostProcessPass     [FinalOutput]     VP×VP RGBA8
Stage 5─────────────────────────────────────────────
  FXAAPass            [FXAA]            VP×VP RGBA8
  UIPass              [FXAA] LOAD       UI 叠加
```

### 6.2 ShadowPass

```
资源: 写入 ShadowMap (2048×2048, Depth Only)
Push: LightSpaceMatrix (mat4) + Transform (mat4)
执行: 遍历 CastShadows=true 的实体，逐个 DrawIndexed
      Fragment Shader 为空 (仅写深度)
      无实例化
```

### 6.3 GBufferPass — 最复杂的 Pass

```
资源: 写入 GBuffer (5 附件):
  Attach 0: RG16F  — Normal (octahedral encoded) + 预留
  Attach 1: RGBA8 — Albedo + Roughness
  Attach 2: RGBA8 — Metallic + AO + flags
  Attach 3: RGBA8 — receiveShadows + isSelected + clipZ + 预留
  Attach 4: Depth

Push: Transform + PBR scalars + Texture usage flags (7 个 map enable bits)

三阶段执行:
  Phase 1 — 扫描: 在排序好的 RenderQueue 中找出连续相同 (Mesh, MaterialHash) 的批次
                   排除单实例条目
  Phase 2 — 实例化: 每组用一次 DrawIndexedInstanced(count, firstInstance)
                   Transform 数组上传到 SSBO (Set=2 Binding=0)
                   材质纹理在 MaterialHash 变化时才重新绑定
  Phase 3 — 逐对象: 单实例 + 非可实例化对象，传统单 DrawIndexed
                   材质属性逐 Property 遍历绑定

⚠️ GDR Shader: gbuffer_gdr.vert 已编译但未使用 (等待 bindless fragment shader)
```

**优化点 #7:** Phase 3 (逐对象绘制) 和 Phase 2 (实例化) 之间的切换是隐式的——如果大部分 mesh 只出现 1 次，实例化没有收益但仍有 Phase 1 的扫描开销。可添加启发式: 如果单实例占比 > 80%，跳过 Phase 1+2。

### 6.4 SSAOPass

```
3 个子 Pass (全部半分辨率):

Generate:
  └─ 32 样本半球采样，TBN 随机旋转
  └─ 内部 m_RawFBO (R8, 半分辨率)

Blur X → m_BlurXFBO:
  └─ 9-tap 双边模糊 (深度 + 法线权重)
  └─ 水平方向

Blur Y → SSAO_Final:
  └─ 9-tap 双边模糊 (垂直方向)
  └─ 输出到 RenderGraph 管理的 FBO

⚠️ 内部 FBO (m_RawFBO, m_BlurXFBO) 不归 RenderGraph 管理
   手动做 layout transition
```

**优化点 #8:** SSAO 生成是 32 样本半球采样 per pixel——这是经典的 O(N²) 操作。优化方向:
- 使用 Compute Shader + shared memory 缓存深度/法线
- Interleaved Rendering (每帧只做 1/N 样本，TAA 累积)
- GTAO (Ground Truth AO) 替代传统 SSAO

### 6.5 LightingPass

```
资源: 读取 GBuffer (4 attach + Depth) + ShadowMap + SSAO_Final
     写入 Lighting (RGBA16F + Depth)
Push: LightSpaceMatrix + AmbientColor + Intensity + EnvMapEnabled + EnableSSAO + InverseViewProj
执行: 全屏三角形 (DrawArrays(3))
     重建世界坐标 (depth + inverseVP)
     解码 Octahedral Normal
     PBR: Directional Light (含 shadow PCF 3×3) + 4 Point Lights + IBL + SSAO
```

**优化点 #9:** 当前 point light culling 仅在 CPU 端做粗略剔除。所有 4 个点光源的 shading 在全屏范围内计算，即使光源影响半径很小。优化:
- Tile-based Light Culling (Compute Shader pre-pass)
- 或者在 Fragment Shader 中做 early-out (距离检查)

### 6.6 ForwardBlendPass

```
资源: ReadWrite Lighting (LOAD, 不清除)
三个子渲染 (共享一个 RenderPass):

1. Skybox (depth LEqual, no write)
2. Grid (2000×2000 plane, alpha blend, Editor Only)
3. Sprites (Painter's Algorithm far→near, per-quad DrawTriangleStrip(4))

⚠️ 特殊 Barrier 处理:
  因为 ForwardBlend LOAD 到 Lighting depth 上，depth 已在 DEPTH_STENCIL_ATTACHMENT 布局。
  执行后手动 barrier: DEPTH_STENCIL_ATTACHMENT → DEPTH_STENCIL_READ_ONLY
  (RenderGraph 的 InsertTileResolveBarrier 对 color+depth 纹理跳过 depth 转换)
```

**优化点 #10:** Sprite 渲染无实例化——每个 sprite 独立调用 `DrawTriangleStrip(4)`。大量 2D 元素时 draw call 数量线性增长。可以用实例化 quad + SSBO transform 数组 (类似 GBuffer Phase 2)。

### 6.7 WBOITPass — 最高级的 Pass

```
资源:
  Gather: 读取 GBuffer (depth, 共享), 写入 WBOIT_Gather
  Resolve: 读取 WBOIT_Gather, ReadWrite Lighting

Gather:
  ⚠️ 直接调用 vkCmdBeginRendering (绕过 RenderCommandBuffer::BeginRenderPass)
  两个 Color Attachment: Accumulation (RGBA16F) + Revealage (RG16F)
  Depth Attachment: LOAD_OP_LOAD + DEPTH_STENCIL_READ_ONLY_OPTIMAL (共享 GBuffer depth)
  Per-attachment Blend:
    [0] Additive blend (accumulation)
    [1] WBOITRevealage (自定义混合模式)
  仅支持实例化路径 (无逐对象 fallback!)
  SSBO 实例化 Transform (max 2048)
  IBL (irradiance + prefiltered + BRDF LUT) 在 WBOIT fragment shader 中独立计算

Resolve:
  全屏三角形: Accumulation / Revealage 合成到 Lighting
  Pre-exposure 补偿: 0.01 缩放因子 → recovery 1/0.01

⚠️ 如果无半透明对象，整个 WBOIT Gather+Resolve 被跳过
  但 skips 的是 Execute 内容，Pass 仍然在 RenderGraph 中注册
```

**优化点 #11:** WBOIT 的 pre-exposure 缩放 (0.01) 是为了避免 FP16 accumulation buffer 溢出。这个 magic number 可能导致:
- 极亮半透明物体在 accumulation 中精度损失
- 可改用 `VK_FORMAT_R16G16B16A16_SFLOAT` 或改用基于深度的 OIT (如 Moment-Based OIT)

### 6.8 OutlinePass

```
资源: 写入 Selection (RGBA8) — 纯白遮罩
执行: 对选中实体递归渲染 (深度测试关闭 → 全轮廓)
      Mesh silhouette + Sprite silhouette
      Pipeline: NoTextureDescriptors=true

⚠️ 深度测试关闭 → 被遮挡的选中物体也会渲染
```

**优化点 #12:** 当前 outline 基于 selected entity 的几何遮罩 + PostProcess 中的 Sobel 边缘检测。这会在物体被遮挡时产生完整的遮挡轮廓。可考虑:
- 对遮挡部分使用 Stencil buffer
- 或者读取 depth 做深度感知的边缘检测

### 6.9 BloomPass

```
5 级降采样 + 4 级升采样 (共 9 个子 Pass):

Downsample Chain:
  Lighting(满分辨率) → Mip0(1/2) → Mip1(1/4) → Mip2(1/8) → Mip3(1/16) → Mip4(1/32)
  13-tap 下采样 filter; Mip0 做 threshold-knee 提取

Upsample Chain:
  Mip4 → Mip3 → Mip2 → Mip1 → Bloom(1/2)
  9-tap Gaussian 上采样; Additive blend

⚠️ 5 个中间 FBO (Mip0-Mip4) 不归 RenderGraph 管理
   手动 layout transition 每个 mip
```

**优化点 #13:** 9 个子 Pass 每个都有独立的 `BeginRenderPass`/`EndRenderPass`。对于 Bloom 这种纯全屏操作，可以考虑:
- 将多次 upsample 合并为一个 Compute Shader (shared memory 积累)
- 或者使用 `vkCmdBlitImage` 做硬件加速的下采样

### 6.10 PostProcessPass

```
资源: 读取 Lighting + Selection + Bloom
     写入 FinalOutput (RGBA8)
执行: 全屏三角形单 Pass
     Bloom compositing → Exposure → Tone Mapping (ACES/Reinhard) → Gamma (1/2.2)
     Selection outline (Sobel 边缘检测 on Selection mask)

所有 texture 输入都有 fallback (WhiteTexture/BlackTexture)
```

### 6.11 FXAAPass & UIPass

```
FXAA: 读取 FinalOutput → 写入 FXAA → 全屏 LDR 抗锯齿
UI:   ReadWrite FXAA (LOAD) → Bindless texture indexing → Premultiplied Alpha blend

UI 批次系统:
  - 10000 quads/frame max, 16 纹理槽位
  - 三帧 VB GC 队列 (m_VBGCTrash[frameIndex])
  - 纹理变化触发 Flush
```

---

## 7. Shader 架构

### 7.1 Shader 文件组织

```
assets/Editor/shaders/src/vulkan/
├── 2D/          sprite.vert/.frag
├── Debug/       debug, pbr_forward
├── Deferred/    gbuffer*.vert, gbuffer.frag, deferred_lighting
├── Fallback/    fallback (magenta debug)
├── IBL/         equirectangular→cubemap, irradiance, prefilter, brdf
├── PostProcess/ bloom_downsample/upsample, clear, fxaa, postprocess
├── Preview/     preview, preview_pbr (AssetPreviewer)
├── SSAO/        ssao_generate, ssao_blur
├── Shadow/      shadow_map
├── Skybox/      skybox
├── UI/          grid, outline, selection_mask, ui
└── WBOIT/       wboit_gather, wboit_resolve, wboit_gather_instanced

总计: 49 个 Shader 文件，无 Compute Shader (.comp)
```

### 7.2 PBR 材质模型

```
Cook-Torrance GGX 微面元 BRDF:
  D = GGX(N, H, roughness²)
  G = Smith = G_SchlickGGX(N,V) × G_SchlickGGX(N,L)   (k = (r+1)²/8)
  F = Schlick: F0 + (1-F0) × (1-cosθ)⁵
      F0 = lerp(0.04, Albedo, Metallic)

  Diffuse  = (1-F) × (1-Metallic) × Albedo / π
  Specular = (D × G × F) / (4 × NdotV × NdotL)

IBL (Split-Sum):
  DiffuseIBL  = kD × Albedo × irradiance × intensity
  SpecularIBL = prefilteredColor × (F_IBL × brdfLUT.r + brdfLUT.g)
  Ambient     = (DiffuseIBL + SpecularIBL) × AO × SSAO

点光源:
  Attenuation = 1 / (d² + ε)
  Windowing    = (1 - (d/radius)⁴)^(falloff+1)  ← UE4 风格平滑衰减
```

### 7.3 GBuffer 编码

```
g_Normal (RG16F):    Octahedral Encoded World-Space Normal
g_Albedo (RGBA8):    RGB=BaseColor, A=Roughness
g_PBR (RGBA8):       R=Metallic, G=AO, B=Flags, A=预留
g_CustomData (RGBA8): R=ReceiveShadows, G=IsSelected, B=ClipZ, A=预留
```

### 7.4 纹理使用标志 (Per-Draw Push Constants)

```
u_UseAlbedoMap     — 是否有 Albedo 贴图
u_UseNormalMap     — 是否有法线贴图
u_UseORMMap        — 是否有 ORM 打包贴图 (R=AO, G=Roughness, B=Metallic)
u_UseMetallicMap   — 是否有独立 Metallic 贴图
u_UseRoughnessMap  — 是否有独立 Roughness 贴图
u_UseAOMap         — 是否有独立 AO 贴图
u_UseAlphaMap      — 是否有 Alpha 贴图 (Masked blend mode)

⚠️ ORM 优先: 当 UseORMMap=1 时，UseMetallicMap/UseRoughnessMap/UseAOMap 被设为 0
```

### 7.5 Shadow Mapping

```
单一 Cascaded Shadow Map:
  Directional Light 正交投影 (radius=20, near=1, far=50)
  3×3 PCF 滤波
  Vulkan Y-flip 补偿: p.y = p.y × (-0.5) + 0.5
  Per-object receiveShadows 标志 (g_CustomData.r)
```

**优化点 #14:** 单一 Shadow Map 无级联 (CSM)——大场景远处阴影精度不足。可实施 3-4 级 CSM。

---

## 8. 材质与资产系统

### 8.1 BakedPC 预烘焙 Push Constants

```cpp
struct BakedPC {
    glm::vec4 Albedo;
    float Metallic, Roughness, AO;
    float Alpha;
    int UseAlbedoMap, UseNormalMap, UseORMMap;
    int UseMetallicMap, UseRoughnessMap, UseAOMap;
    // 预解析的纹理指针 (6 个 Texture2D shared_ptr)
    std::shared_ptr<Texture2D> AlbedoMap, NormalMap, ORMMap,
                               MetallicMap, RoughnessMap, AOMap;
    bool Dirty;  // 属性变化时触发 lazy re-bake
};
```

**优化点 #15:** `BakedPC::Dirty` 标志在每帧 `Bind()` 时检查。对于材质属性频繁变化的场景，可以考虑增量更新而非全量 `BakeProperties()`。

### 8.2 资产热重载 (AssetWatcher)

```
轮询 mtime → 300ms debounce → 拓扑排序 (0=texture, 1=material, 2=prefab, 3=scene)
→ 级联重载依赖 → 3 帧延迟释放 (AssetManager::s_DeferredReleases)
```

### 8.3 GC (Mark & Sweep)

```
Scene::GetActiveAssetHandles() → 遍历所有实体组件
→ AssetManager::UnloadUnusedAssets() → 卸载非内置资产
```

---

## 9. 深度优化机会评估

以下优化按 **影响程度** 和 **实现难度** 分级。

### 🔴 Tier 1 — 高影响、中低难度

| # | 优化项 | 问题 | 方案 | 预期收益 |
|---|--------|------|------|---------|
| **9.1** | **Material Textures → Bindless** | 当前每 draw 做 `vkUpdateDescriptorSets` (Set 1)，大量重复 descriptor write。GBuffer/Lighting/WBOIT 全部使用传统 Set=1 | 将 material textures 全部迁移到 bindless 纹理数组。Per-draw 仅 push 一个 bindless index 数组 | Draw call 开销降低 30-50%，CPU 瓶颈场景显著提升 |
| **9.2** | **Tile-Based Light Culling** | 4 个点光源全屏计算，即使光源影响半径远小于屏幕 | Compute Shader pre-pass 做 frustum-aligned tile culling，生成 per-tile light list | Lighting Pass 的 Fragment Shader 计算量降低 50-80% (多光源场景) |
| **9.3** | **Shadow Map: 单级 → CSM 3-4 级** | 单张 Shadow Map 2048 → 大场景阴影精度严重不足 | 实现 3-4 级 Cascaded Shadow Maps，按相机距离分配分辨率 | 阴影质量质的飞跃，且近处阴影精度大幅提升 |
| **9.4** | **Mipmap 生成** | 所有纹理 mipLevels=1，远处物体采样效率极低 | 上传纹理后自动生成 mipmap chain (`vkCmdBlitImage`) | Cache 命中率提升，消除远处锯齿/摩尔纹，带宽降低 |

### 🟡 Tier 2 — 中影响、中等难度

| # | 优化项 | 问题 | 方案 | 预期收益 |
|---|--------|------|------|---------|
| **9.5** | **GBuffer 深度预 Pass** | GBuffer Pass 直接写 5 个附件，fragment shader 包含完整的 PBR 材质计算。很多 fragment 会被深度测试拒绝 | 增加一个轻量 Depth-Only Pre-Pass，只写深度。GBuffer Pass 设置 depth compare = EQUAL | 减少 GBuffer fragment shader 执行量 (取决于 overdraw)，尤其复杂场景 |
| **9.6** | **RenderGraph Pass Culling** | `RGPass::IsCulled` 已预留但未实现。禁用 SSAO/Bloom/FXAA 时 Pass 仍然注册+执行空回调 | 实现 IsCulled 逻辑：如果 Pass 的输出无人消费或输入为空，从 DAG 剔除 | 功能禁用时节省 GPU 时间 + 屏障开销 |
| **9.7** | **Instance 化 Sprite 渲染** | ForwardBlendPass 的 Sprite 每 quad 独立 `DrawTriangleStrip(4)` | 使用 SSBO 实例化 (同 GBuffer Phase 2)，批量提交 sprite transforms | 大量 2D 元素时 draw call 数量降低 100-1000x |
| **9.8** | **SSAO → GTAO 升级** | SSAO 使用传统 32-sample 半球采样，缺乏时空稳定性 | 实现 GTAO (Ground Truth AO) 或 CACAO | AO 质量提升 + 可能性能改善 (更少样本) |
| **9.9** | **Compute Shader 后处理** | SSAO/Bloom 全部用 Fragment Shader 全屏三角形实现。Bloom 需要 9 个子 Pass | 将 SSAO Blur/Bloom Upsample 用 Compute Shader + shared memory 合并 | GPU 利用率提升，减少 RenderPass 切换开销 |

### 🟢 Tier 3 — 低影响或高难度

| # | 优化项 | 问题 | 方案 | 预期收益 |
|---|--------|------|------|---------|
| **9.10** | **GPU-Driven Rendering** | 当前在 CPU 做 frustum culling + draw call 生成。gbuffer_gdr.vert 已编写但未使用 | 完成 GPU-Driven 路径：GPU frustum/occlusion culling → indirect draw。需要 Compute Shader | CPU 瓶颈场景大幅改善，但工程量大 |
| **9.11** | **Variable Rate Shading** | 全屏统一 shading rate | VRS Tier 1/2: 对低对比度区域降低 shading rate | 10-30% fragment shading 节省 (需硬件支持) |
| **9.12** | **Async Compute** | 所有 Pass 在 Graphics Queue 串行执行 | SSAO/Bloom 移到 Async Compute Queue 与 GBuffer/Lighting 并行 | GPU 占用率提升 10-20% |
| **9.13** | **Mesh Shading** | 传统 Vertex Shader pipeline | 替换为 Mesh Shader (VK_EXT_mesh_shader) | GPU 驱动的几何处理，更灵活的 LOD |
| **9.14** | **Bindless Material 全量迁移** | 仅 UI 使用 bindless | 所有 material textures (GBuffer/Lighting/WBOIT) 使用 bindless + `nonuniformEXT` | Set=1 描述符写入完全消除 |

### 🔵 架构改进 (非性能)

| # | 改进项 | 问题 | 方案 |
|---|--------|------|------|
| **9.15** | **Depth Layout 双轨追踪** | `RGTexture::CurrentLayout` 仅追踪单一布局，color+depth 纹理的 depth 布局可能不准确 | 实现 `ColorLayout` + `DepthLayout` 独立追踪，确保 depth correctly transitioned |
| **9.16** | **EndSingleTimeCommands 异步化** | 一次性命令使用 `vkQueueWaitIdle` + `vkDeviceWaitIdle` 全局阻塞 | 改用 per-transfer Fence + Ring Buffer staging，避免全局 GPU 阻塞 |
| **9.17** | **VulkanForwardTestPass 清理** | 独立的 Forward 渲染路径 (~300 行)，不在活跃管线中 | 删除或合并到 RenderGraph 作为可选 forward pass |
| **9.18** | **Shader Variant 统一** | GBuffer 有 3 个 vertex shader 变体 (single/instanced/gdr)，需手动维护 | 评估是否可通过 specialization constant 合并 |

---

## 总结

### 架构优点

1. **Vulkan 1.3 Dynamic Rendering** — 消除了 VkRenderPass/VkFramebuffer 的复杂性，RenderGraph 自然地处理布局转换
2. **RenderGraph DAG + 自动屏障** — 生产级帧图系统，正确处理 TBDR tile resolve
3. **延迟描述符批量写入** — 减少 `vkUpdateDescriptorSets` 次数
4. **三帧飞行隔离** — UBO/SSBO/FBO/DescriptorSet 全覆盖，健壮的同步模型
5. **VMA 集成** — GPU 内存管理委托给专业库
6. **实例化渲染** — GBuffer 和 WBOIT 的 SSBO-based instancing 有效减少 draw calls
7. **PBR + IBL** — 完整的物理渲染材质模型

### 关键瓶颈评估

| 瓶颈 | 位置 | 严重程度 |
|------|------|---------|
| **Per-Draw Descriptor Write** | GBuffer Phase 3 逐材质纹理绑定，每次 `vkUpdateDescriptorSets` | 高 — CPU 瓶颈场景 |
| **无 Mipmap** | 所有纹理 `mipLevels=1` | 中高 — Cache/带宽 |
| **全屏 Point Light Shading** | LightingPass fragment shader | 中 — GPU 瓶颈 |
| **单一 Shadow Map** | ShadowPass → LightingPass | 中 — 视觉质量 |
| **SSAO 32-sample 半球** | SSAOPass Generate | 中 — GPU 瓶颈 |
| **Bloom 9 子 Pass** | BloomPass | 低中 — RenderPass 切换 |
| **Pass Culling 未实现** | RenderGraph | 低 — 功能禁用时浪费 |

### 建议的优化路线

```
Phase 1 (Quick Wins):
  9.1 Bindless Material → 减少描述符开销
  9.4 Mipmap 生成 → 视觉质量 + Cache
  9.6 Pass Culling → 功能禁用时零开销

Phase 2 (Quality + Performance):
  9.3 CSM Shadow Maps → 阴影质量
  9.2 Tile Light Culling → GPU 计算量
  9.7 Instanced Sprites → Draw call 减少
  9.9 Compute Post-Processing → GPU 并行度

Phase 3 (Advanced):
  9.5 Depth Pre-Pass → 复杂场景优化
  9.10 GPU-Driven Rendering → CPU 解放
  9.12 Async Compute → GPU 占用率
```

---

## 10. Bindless Material Texture — 落地实现

### 10.1 架构原理

传统路径每个 Draw Call 需要 7 次 `BindTexture2D()` + 1 次 `FlushDescriptorSets()` 触发 `vkUpdateDescriptorSets` + `vkCmdBindDescriptorSets`。每个 Pipeline 预分配 3000 个 ring-buffer descriptor sets。

Bindless 方案将全部材质纹理注册到一个全局 `sampler2D[]` 数组中，shader 通过 Push Constants 中的 `uint` 索引直接访问——消除 per-draw descriptor 写入。

### 10.2 核心组件

**VulkanBindlessManager** (`VulkanBindlessManager.hpp:9-30`):

```
固定索引预留:
  kInvalidIndex        = 0  (哨兵值, 永不分配)
  kWhiteIndex          = 1  (1x1 white, 乘性单位元)
  kBlackIndex          = 2  (1x1 black)
  kDefaultNormalIndex  = 3  (1x1 {128,128,255}, 平坦法线)
  kFirstFreeIndex      = 4  (AllocateIndex 起始)

AllocateIndex(): FreeList (LIFO) -> m_NextIndex (线性递增)
FreeIndex(): 仅接受 index >= kFirstFreeIndex (保护固定索引)
UpdateBinding(): vkUpdateDescriptorSets 写入全局 set 的 arrayElement
```

**VulkanContext** (`VulkanContext.cpp:890-940`):

```
CreateDefaultBindlessTextures():
  直接创建 VkImage/VkImageView/VkSampler (1x1 像素, 不走 VulkanTexture2D)
  BeginSingleTimeCommands -> Upload -> EndSingleTimeCommands
  UpdateBinding() 注册到索引 1-3

QueueDeferredBindlessRelease(index):
  推入 3 帧延迟队列 -> ProcessDeferredBindlessReleases() 在 BeginFrame() 中
  fence 确认 GPU 完成后调用 FreeIndex()
```

**BakedPC 重构** (`Material.hpp:123-150`):

```cpp
struct BakedPC {
    glm::vec4 Albedo{1.0f};
    float Metallic=0, Roughness=0.5, AO=1, Alpha=0.5;
    uint32_t UseORMMap = 0;
    uint32_t AlbedoMapIndex   = 1;  // white default
    uint32_t NormalMapIndex   = 3;  // flat normal default
    uint32_t ORMMapIndex      = 2;  // black default
    uint32_t MetallicMapIndex  = 1;
    uint32_t RoughnessMapIndex = 1;
    uint32_t AOMapIndex        = 1;
    bool Dirty = true;

    // 贴图存在时 scalar -> 1.0, shader 中 1.0 * texture = texture
    void GetRenderScalars(float& m, float& r, float& a) const {
        m = (MetallicMapIndex != 1) ? 1.0f : Metallic;
        r = (RoughnessMapIndex != 1) ? 1.0f : Roughness;
        a = (AOMapIndex != 1) ? 1.0f : AO;
    }
};
```

**Shader 改造** (`gbuffer_bindless.frag`, `wboit_gather_bindless.frag`):

```glsl
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 1, binding = 0) uniform sampler2D u_GlobalTextures[];

// 无条件采样 — 默认索引指向有效 1x1 纹理
vec4 albedo = texture(u_GlobalTextures[nonuniformEXT(idx)], uv);

// 全部 Push Constants 使用 vec4/uvec4 打包 (避免 std430 vec3=16B 对齐陷阱)
```

### 10.3 WBOIT IBL 隔离

WBOIT bindless shader 中 IBL 纹理 (IrradianceMap/PrefilteredMap 为 samplerCube) 迁移到独立 set=3:
- Set 0: Camera + LightData UBO
- Set 1: Bindless 2D 数组 (binding=0)
- Set 2: SSBO 实例化 Transforms
- Set 3: IBL 纹理 (bindings 0/1=cube, 2=2D) — 每帧绑定一次

---

## 11. Mipmap 自动生成

### 11.1 实现位置 (`VulkanTexture2D.cpp`)

**Mip 层级计算** (`Invalidate()`):

```cpp
// 压缩格式保护: BCn/ASTC/ETC2 -> m_MipLevels=1
// 格式特性检查: VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
// GenerateMipmaps 设置检查
if (isCompressed || !supportsLinearBlit || !GenerateMipmaps)
    m_MipLevels = 1;
else
    m_MipLevels = floor(log2(max(w,h))) + 1;
```

**VkImageCreateInfo**: `mipLevels = m_MipLevels`, `usage |= TRANSFER_SRC_BIT`
**VkImageViewCreateInfo**: `levelCount = m_MipLevels`
**VkSamplerCreateInfo**: `maxLod = static_cast<float>(m_MipLevels)`, `mipLodBias = 0`

**SetData() 逐级 Blit 循环**:

```
初始 barrier: UNDEFINED -> TRANSFER_DST (levelCount = m_MipLevels, 全部 mip)
for i = 1 to m_MipLevels-1:
  Step A: Mip[i-1] DST -> SRC (vkCmdPipelineBarrier, levelCount=1)
  Step B: vkCmdBlitImage Mip[i-1] -> Mip[i] (VK_FILTER_LINEAR, 硬件缩放)
  Step C: Mip[i-1] SRC -> SHADER_READ_ONLY (本层完成)
Step D: Mip[last] DST -> SHADER_READ_ONLY (最后一层)
```

---

## 12. RenderGraph Pass 剔除

### 12.1 核心机制 (`RenderGraph.cpp`)

**标记阶段** (`SceneRenderer::BuildRenderGraph()`, 每帧):

```cpp
SetPassCulled("SSAOPass", !enableSSAO);
SetPassCulled("BloomPass", !enableBloom);
SetPassCulled("WBOIT_Gather", !hasTranslucent);
SetPassCulled("WBOIT_Resolve", !hasTranslucent);
```

**编译阶段** (`RenderGraph::Compile()`, 每帧):

```
Step 0: active = { p | !p->IsCulled }
Step 1-4: Producer Map -> 依赖图 -> Kahn 排序 -> 全部仅遍历 active
Step 5: FBO 创建 -> 仅 IsWritten=true 的纹理 (culled producer -> 跳过)
```

**Fallback 策略**: 被剔除 Pass 产出的纹理不创建 PhysicalFBO -> `context.GetFramebuffer()` 返回 nullptr -> 消费 Pass 自行检测并绑定 WhiteTexture 替代。

**布局追踪修复**: `CurrentLayout/DepthLayout` 仅在 FBO 首次创建时初始化——不再每帧 Compile 重置 (避免与 GPU 端实际布局失配)。

---

## 13. GPU-Driven Rendering — 四步实现

### 13.1 架构全景

```
CPU (每帧)                 GPU (Compute)              GPU (Graphics)
─────────                  ─────────────              ──────────────
Resource Staging
  -> GetOrUploadMesh()

Build GDR data
  -> GPUInstance[]
  -> GPUMaterial[]  --SetData-->  SSBOs
  -> GeometryRange[]

                        cull.comp                    gbuffer_gdr.vert
                        Gribb-Hartmann               uint g_Data[] 解包
                        6-plane sphere cull          TBN via dFdx/dFdy
                        |
                        Commands[gID]
                        (fixed-slot output)          gbuffer_gdr_bindless.frag
                                                     Materials[] SSBO 直读
                        vkCmdDrawIndexedIndirect
                             ^ 单次调用
```

### 13.2 Step 1 — GPU Scene Data SSBOs

**Set=2 布局** (图形 + compute 共享, 4 bindings):

```
Binding 0: GPUInstance[4096]   SSBO — mat4 transform(64B) + vec4 boundingSphere(16B) + u32x4(16B)
Binding 1: GeometryRange[1024] SSBO — vertexOffset(uint elem) + indexOffset(byte) + counts
Binding 2: GPUMaterial[512]    SSBO — PBR scalars + 6 bindless indices + UseORMMap + flags
Binding 3: uint g_Data[]       SSBO — GlobalGeometryPool 原始字节
```

所有 descriptor 在 `OnAttach()` 预绑定 — 每帧零 `vkUpdateDescriptorSets`。

**`gbuffer_gdr_bindless.frag`** (新建, 80 行): Fragment Shader 直接从 `Materials[]` SSBO 读取全部 PBR 参数 + 6 bindless 索引, 内联 `GetRenderScalars()` 逻辑 (`idx!=1 ? 1.0 : scalar`)。

### 13.3 Step 2 — SSBO Vertex Fetch

**`gbuffer_gdr.vert`** 完全重写:
- 移除 VBO `layout(location=N) in` 输入
- `uint g_Data[]` SSBO, stride = 11 uint/vertex (44B/4)
- `uintBitsToFloat` 解包 Position(3) + Normal(3) + TexCoord(2)
- **Tangent 跳过** — 节省 27% 顶点带宽, TBN 由 fragment shader 通过 `dFdx/dFdy` 重建
- `v_MaterialIdx` flat uint 传递给 FS

**GlobalGeometryPool 扩展**:
- `GetOrUploadMesh(Mesh*)`: 惰性上传到 256MB 池, `m_MeshRanges` O(1) 查找
- `vertexOffset` -> uint 元素索引 (byteOffset/4)
- CPUSide `m_RawVertices/m_RawIndices` 存储 (Mesh.hpp/.cpp)

**Index Buffer**: 全局 `vkCmdBindIndexBuffer(pool, offset=0)` 一次, `firstIndex = range.indexOffset/4` per draw — Post-Transform Vertex Cache 完整保留。

### 13.4 Step 3 — Compute Frustum Culling

**`cull.comp`** (73 行):

```glsl
layout(local_size_x = 64) in;

// 输入: Set 2 Binding 0 (InstanceBuffer), Binding 1 (GeometryRangeBuffer)
// 输出: Set 3 Binding 0 (DrawIndirectBuffer)
// Push Constants: u_Planes[6] (Gribb-Hartmann) + u_InstanceCount

void main() {
    uint gID = gl_GlobalInvocationID.x;
    if (gID >= pc.u_InstanceCount) return;

    if (IsVisible(Instances[gID].boundingSphere)) {
        Commands[gID] = { indexCount, 1, firstIndex, 0, gID };
    } else {
        Commands[gID].instanceCount = 0;  // GPU 硬件 skip
    }
}
```

**固定槽位方案**: 不使用 atomic counter, 不使用 `drawIndirectCount` 扩展 — MoltenVK/M1 兼容。剔除的实例写 `instanceCount=0`, 硬件层面跳过。

**Compute Pipeline 创建** (`VulkanGBufferPass.cpp` OnAttach):
- SPIR-V 直接从文件加载 (`std::ifstream` -> `vkCreateShaderModule`)
- Pipeline Layout: Set=2 (GDR) + Set=3 (DrawCommands), 4 元素数组 (0/1=空布局占位)
- Push Constants: COMPUTE_BIT, 112 bytes (6xvec4 + uint + pad)

### 13.5 Step 4 — Indirect Draw

**旧代码退役**: `GetBatchKey/IsInstancable/FillPC` 静态函数 + Phase 1/2/3 (~180 行) 全部删除。
`GBufferPushConstants` struct 删除。12 个旧管线成员从 header + OnAttach 移除。

**Execute() 单一路径** — Phase 4:

```
1. Resource Staging: for pkt -> pool.GetOrUploadMesh() (CPU memcpy, HOST_COHERENT)
2. 从 RenderQueue 构建 GPUInstance[] + GPUMaterial[] + GeometryRange[]
3. SetData() -> persistent-mapped SSBO (triple-buffered)
4. vkCmdDispatch cull.comp (render pass 外)
5. vkCmdPipelineBarrier COMPUTE_WRITE -> DRAW_INDIRECT
6. cmd.BeginRenderPass (无条件 — 空场景时清除 GBuffer 防 ghost)
7. vkCmdBindIndexBuffer(pool, offset=0)
8. vkCmdBindDescriptorSets(set=2)
9. vkCmdDrawIndexedIndirect (单次调用, stride=sizeof(VkDrawIndexedIndirectCommand))
```

---

## 14. 材质系统清理

- 移除全部 `u_UseXxxMap` Bool 属性 (BakedPC, PropertiesPanel, MaterialSerializer, AssetManager, DefaultPBR.mat)
- 保留 `u_HeightMap`/`u_EmissiveMap` 纹理槽位 (未来实现预留)
- 移除 `u_UseHeightMap`/`u_UseEmissiveMap` Bool 开关
- 贴图是否存在由 `tex->GetBindlessIndex() != 0` 判定 (BakeProperties)

---

## 15. Bug 修复清单

| Bug | 根因 | 修复位置 |
|-----|------|---------|
| Set 0 Layout 泄漏 | 析构 `!UseBindlessTextures` 守卫了 UBO layout | VulkanPipeline.cpp — 分拆 Set 0/1 |
| Bindless Layout 双重销毁 | `NoGlobalUBOs` 时 Set 0=bindless layout | bindlessInSet0/1 两路条件 |
| SSBO Set 2 未绑定 | 用旧 layout 绑 Set 2 后切 bindless 管线 | 先 BindPipeline 再 bind Set 2 |
| Bindless 容量超限 | WBOIT set=3 IBL samplers 未预留 | maxBindless -= 32 |
| Mip 1+ UNDEFINED | 初始 barrier levelCount=1 | levelCount=m_MipLevels |
| Compute layout 索引不匹配 | setLayoutCount=2 但 shader set=2/3 | 4 元素数组含空布局占位 |
| Compute dispatch inside RP | 动态渲染内不可调 vkCmdDispatch | 移到 BeginRenderPass 之前 |
| Ghost 渲染 | 空场景时 BeginRenderPass 未调用 | 无条件清除 GBuffer |
| VkShaderModule 泄漏 | compModule 局部变量未销毁 | 存储 m_Cull_ShaderModule |
| drawIndirectCount 不支持 | MoltenVK/M1 不支持此特性 | 移除 feature 请求 + 固定槽位方案 |

---

## 16. 更新后的优化评估

### 已完成的优化

| 优化项 | 原始评估 | 落地状态 |
|--------|---------|---------|
| 9.1 Bindless Material | Tier 1 — 高影响 | ✅ 完成 — 全局 sampler2D[] + Push Constant 索引 |
| 9.4 Mipmap 生成 | Tier 1 — 高影响 | ✅ 完成 — vkCmdBlitImage 逐级, 压缩格式保护 |
| 9.6 Pass Culling | Tier 1 — 高影响 | ✅ 完成 — Compile 阶段 DAG 剪枝 |
| 9.2 Tile Light Culling | Tier 2 — 中等 | ⚠️ 部分 — Point light 距离 Shader 早期退出 (无 Compute Tile) |
| 9.3 CSM Shadow Maps | Tier 1 — 高影响 | ❌ 未实现 |
| 9.10 GPU-Driven Rendering | Tier 3 — 高级 | ✅ 完成 — 四步全部落地, 含 Compute Culling + Indirect Draw |

### 新增优化机会

| 优化项 | 说明 |
|--------|------|
| Compute Tile Light Culling | 用 Compute Shader 预计算 per-tile light list (替代当前全屏 point light 循环) |
| Hi-Z 遮挡剔除 | 生成深度金字塔 + Compute occlusion test -> cull.comp 可扩展 |
| Mesh 预上传到 GeometryPool | 场景加载时批量上传 (当前惰性触发, 首帧可能卡顿) |
| TextureCube Mipmap | IBL 环境贴图预过滤时一并生成 mip 层级 |
| u_AlphaMap 接入 | BakeProperties 中添加 AlphaMapIndex 解析 |

### 性能影响总结

| 指标 | 改进 |
|------|------|
| CPU Draw Call 开销 | `vkUpdateDescriptorSets` 消除, FillPC() 移除, 间接绘制替代 CPU 循环 |
| GPU ALU | Point light 距离剔除, Compute frustum culling 并行化 |
| 顶点带宽 | Tangent 跳过节省 27% |
| 纹理 Cache | Mipmap 链减少远处纹理 Cache Miss |
| 描述符池内存 | Bindless pipelines 0 COMBINED_IMAGE_SAMPLER, 旧 ring buffers 移除 |
| 代码复杂度 | 12 个旧管线成员 + ~180 行 Phase1-3 代码删除 |

---

## 17. GDR Bug 排查与修复记录

> **排查期间:** 2026-06-18 ~ 2026-06-20 | **状态:** ✅ 面撕裂根因已定位并修复 (Bug H)，9 项 Bug 全部修复，诊断代码已清理

### 17.1 已修复的 Bug

#### 🔴 Bug A — Frustum Plane Column/Row 错位

**文件:** `VulkanGbufferPass.cpp:351`

**根因:** `glm::mat4` 是 column-major 存储，`vp[i]` 返回第 i 个**列**。但 Gribb-Hartmann 方法需要**行**。列向量组合 ≠ 行向量组合，near/far 平面完全错误。

```cpp
// Bug: vp[i] returns COLUMNS, not rows
glm::vec4 rows[4] = { vp[0], vp[1], vp[2], vp[3] };

// Fix: transpose first so vpT[i] returns row i
glm::mat4 vpT = glm::transpose(vp);
glm::vec4 rows[4] = { vpT[0], vpT[1], vpT[2], vpT[3] };
```

**验证:** CPU 侧 `Frustum` 类（`Frustum.hpp`）使用显式 `viewProjection[col][row]` 双索引逐元素提取，正确实现了 Gribb-Hartmann。两者修复后等价。

**症状:** 相机内物体被错误剔除、边界闪烁。

---

#### 🔴 Bug B — `drawIndirectFirstInstance` 未启用

**文件:** `VulkanContext.cpp:223-226`

**根因:** cull.comp 为每个子网格设置独立 `firstInstance = gID`：
```glsl
Commands[gID].firstInstance = gID;
```
但 Vulkan 规范规定：未启用 `drawIndirectFirstInstance` 时，`firstInstance` 被**忽略**，强制为 0。全部子网格共享 `Instances[0]` 的 transform + geometryRangeIdx + materialIdx，但各自使用不同的 index buffer offset → 垃圾三角形。

```cpp
// Fix: add to VkPhysicalDeviceFeatures
deviceFeatures.drawIndirectFirstInstance = VK_TRUE;
```

**症状:** 模型面撕裂（所有子网格读 Instance[0] 的顶点区）、Cube 闪烁。

---

#### 🔴 Bug C — `multiDrawIndirect` 未启用

**文件:** `VulkanContext.cpp:225`

**根因:** `vkCmdDrawIndexedIndirect` 的 `drawCount` = `instanceCount` (如 79)。Vulkan 规范：未启用 `multiDrawIndirect` 时 `drawCount` 只能为 0 或 1。

```cpp
// Fix: add to VkPhysicalDeviceFeatures
deviceFeatures.multiDrawIndirect = VK_TRUE;
```

**症状:** Validation Error VUID-vkCmdDrawIndexedIndirect-drawCount-02718。

---

#### 🔴 Bug D — GeometryPool 缺少 VMA HOST_COHERENT

**文件:** `VulkanGeometryPool.cpp:23-26`

**根因:** `VmaAllocationCreateInfo` 缺少 `requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`。对比 `VulkanStorageBuffer` 正确设置。non-coherent 内存上 `memcpy` 后 GPU 可能读到未刷新数据。

```cpp
// Fix: add requiredFlags
allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
```

**症状:** 顶点/索引数据损坏 → 面撕裂（取决于硬件内存类型）。

---

#### 🟡 Bug E — 包围球半径未乘 World Scale

**文件:** `VulkanGbufferPass.cpp:322`

**根因:** 局部空间半径直接使用，未乘以变换矩阵的 maxScale。CPU 侧 `Frustum::IsBoxVisible` 正确实现了 `maxScale` 乘法。

```cpp
// Fix: extract scale from transform columns
glm::vec3 scale(
    glm::length(glm::vec3(pkt.Transform[0])),
    glm::length(glm::vec3(pkt.Transform[1])),
    glm::length(glm::vec3(pkt.Transform[2]))
);
float maxScale = glm::max(scale.x, glm::max(scale.y, scale.z));
float radius = glm::length(aabb.Max - aabb.Min) * 0.5f * maxScale;
```

**症状:** scale ≠ 1 的实体被错误剔除。

---

#### 🟡 Bug F — 空场景不清理 GBuffer (Ghost 渲染)

**文件:** `VulkanGbufferPass.cpp:243-244`

**根因:** `queue->Packets.empty()` 时直接 return，未调用 `BeginRenderPass`。上一帧 GBuffer 残留。

```cpp
// Fix: unconditionally clear GBuffer
if (!queue || queue->Packets.empty()) {
    cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
    cmd.EndRenderPass();
    return;
}
```

---

#### 🟢 Bug G — 子实体未注册到父 Children 列表

**文件:** `Scene.cpp:162-179`

**根因:** `InstantiateModelNode` 多 mesh 分支中，子实体未加入 `pRel.Children`。

---

#### 🔴 Bug H — Pipeline Topology 错误：TRIANGLE_STRIP 替代 TRIANGLE_LIST **(面撕裂根因)**

**文件:** `VulkanPipeline.cpp:108`

**根因:** `VulkanPipeline` 的 `inputAssembly.topology` 映射中，当 `PrimitiveTopology::Triangles`（默认）且 `attributeDescriptions` 为空时，被错误映射为 `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP`：

```cpp
// Bug: empty vertex layout → TRIANGLE_STRIP (heuristic for procedural quads)
default: inputAssembly.topology = attributeDescriptions.empty()
    ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
    : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

// Fix: explicit Triangles case, always TRIANGLE_LIST
case PrimitiveTopology::Triangles: inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
```

GDR pipeline 使用 `Layout = {}`（SSBO vertex pulling 不需要 VBO），导致 `attributeDescriptions.empty() == true`。Mesh 索引缓冲区按 `TRIANGLE_LIST` 构建，但 GPU 按 `TRIANGLE_STRIP` 解析——每 3 个顶点后，下一个三角形复用前一个三角形的最后 2 个顶点。结果：三角形拉伸错位、模型"面撕裂"。

**联动修复:** 同步更新了 9 个依赖隐式启发式规则的全屏 pass（FXAA / Bloom / Lighting / SSAO / WBOIT / PostProcess / ForwardBlend / ForwardTest / Outline），显式设置 `Topology = PrimitiveTopology::TriangleStrip`。`VulkanOutlinePass` 已显式设置，无需修改。

**症状:** 所有 GDR 渲染的模型出现面撕裂（backpack.obj 79 子网格三角形拉伸错位）、Cube 单三角闪烁。

---

#### 🟡 Issue I — vkCtx/m_GDRPipeline 为空时缺少 BeginRenderPass

**文件:** `VulkanGbufferPass.cpp:264-270`

**根因:** 当 `vkCtx` 或 `m_GDRPipeline` 为空时，整个 GDR 代码块被跳过，但 `cmd.EndRenderPass()` 在行 432 始终被调用——导致 `vkCmdEndRenderPass()` 在没有活跃 render pass 的情况下调用，触发 Vulkan validation error。

```cpp
// Fix: guard clause with explicit BeginRenderPass + EndRenderPass + return
if (!vkCtx || !m_GDRPipeline) {
    cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
    cmd.EndRenderPass();
    context.Framebuffers["GBuffer"] = fbo;
    return;
}
```

**症状:** Validation Error `VUID-vkCmdEndRenderPass-None-00951`（无活跃 render pass 时调用 EndRenderPass）。

---

#### 🟢 附加改进

- **SSBO 上限裁剪:** `VulkanGbufferPass.cpp` — `std::min(count, kMaxCapacity)` 防溢出
- **GeometryPool 越界检查:** `VulkanGeometryPool.cpp` — `m_Cursor + required > m_Size` 防护 + 错误日志
- **结构体大小静态断言:** `VulkanGeometryPool.hpp` — 三个 `static_assert` 锁定 C++/GLSL 对齐

---

### 17.2 已排除的嫌疑人

| # | 嫌疑人 | 排查方法 | 结论 |
|---|--------|---------|------|
| 1 | 顶点格式对齐 (VBO 44B vs SSBO 11uint) | 逐 byte 对比 BufferLayout vs Vertex struct | ✅ 一致 |
| 2 | SPIR-V 编译错误 | `spirv-dis` 反汇编验证 `OpUDiv`/`OpIMul`/struct offset/ArrayStride | ✅ 正确 |
| 3 | 整数溢出 (256MB 池 / 79 子网格) | 计算 `indexOffset/4` 在 uint32 安全范围 | ✅ 安全 |
| 4 | 16-bit vs 32-bit 索引类型不匹配 | 全链路追踪：Assimp(`unsigned int*`) → Model(`uint32_t`) → Mesh(`uint32_t`) → Pool(`sizeof(uint32_t)`) → Vulkan(`UINT32`) | ✅ 统一 32-bit |
| 5 | 子网格 Global Index 双偏移 | 验证 Assimp aiMesh 索引为 local 0-based，ProcessMesh 只在 MergeMeshes 时添加 vertOffset | ✅ 无叠加 |
| 6 | 双重渲染 Z-Fighting | Phase 1-3 已物理删除，`SceneRenderer::RenderScene()` 严格 if-else 隔离 OpenGL/Vulkan | ✅ 无并行 |
| 7 | Triple-Buffer Descriptor 追尾 | 每帧独立 VkBuffer + 预绑定 Set + 一致 frameIdx + 正确 Barrier + `vkWaitForFences` | ✅ 无竞争 |
| 8 | GPUInstance/GPUMaterial 内存偏移雪崩 | C++ `alignas(16)` + `_pad` 成员 → sizeof vs GLSL std430 ArrayStride | ✅ 全部一致 |

### 17.3 C++/GLSL 结构体内存对齐验证

| 结构体 | C++ sizeof | GLSL std430 ArrayStride | static_assert |
|--------|-----------|------------------------|---------------|
| `GeometryRange` | 16 | 16 | ✅ |
| `GPUInstance` | 96 | 96 | ✅ |
| `GPUMaterial` | 112 | 112 | ✅ |

---

### 17.4 已启用的 VkPhysicalDeviceFeatures

```cpp
deviceFeatures.independentBlend = VK_TRUE;               // WBOIT per-attachment blend
deviceFeatures.multiDrawIndirect = VK_TRUE;              // GDR: drawCount > 1
deviceFeatures.drawIndirectFirstInstance = VK_TRUE;      // GDR: firstInstance != 0 per draw
```

---

### 17.5 当前状态

| 症状 | 状态 |
|------|------|
| **模型面撕裂** | ✅ **Bug H (Topology) 为根因** — TRIANGLE_STRIP→TRIANGLE_LIST 修复 |
| **Cube 单三角闪烁** | ✅ 同上，Topology 修复后消失 |
| **诊断代码** | ✅ 已清理 — cull.comp 恢复剔除，gbuffer_gdr_bindless.frag 恢复 PBR |
| **Editor 视口无物体** | ✅ **Bug I (GDRContext 创建两次)** — `s_GDRContext` 被第二次 Init 覆盖，Editor 视口的 pass 持有僵尸 context |
| **VK_ERROR_DEVICE_LOST** | ✅ **资产缓存在热重载后过期** — 添加 `Scene::InvalidateAssetCache()` 事件驱动作废 |


### 17.6 诊断改动（已全部撤销）

以下诊断改动已在 2026-06-20 清理：

- **cull.comp — 临时禁用 Frustum Culling:** ~~`IsVisible()` 调用已移除~~ → **已恢复**完整视锥体剔除逻辑（`IsVisible()` + 可见性分支）
- **gbuffer_gdr_bindless.frag — UV 诊断颜色:** ~~输出 `v_TexCoord` + `v_MaterialIdx` 作为颜色~~ → **已恢复**完整 bindless PBR 渲染管线（纹理采样、法线贴图、ORM 分支、alpha-cutout）
- **VulkanGbufferPass.cpp — 每 60 帧日志:** ~~`static int s_GDRFrameCounter` + `AYAYA_CORE_INFO`~~ → **已移除**

### 17.7 下一步

- [x] **验证 Topology 修复** — ✅ 已确认
- [x] **CPU 视锥体剔除消除** — ✅ Blind Submit 落地 (Section 18.1)
- [x] **Transform 缓存** — ✅ 哈希缓存落地 (Section 18.2)
- [x] **资产指针缓存** — ✅ 事件驱动作废落地 (Section 18.4)
- [x] **u_AlphaMap 管线接入** — ✅ 落地 (Section 18.5)
- [ ] **Mesh 预上传优化** — 场景加载时批量上传到 GeometryPool
- [ ] **Hi-Z 遮挡剔除** — 在 cull.comp 前插入深度金字塔 + Compute occlusion test
- [ ] **GPUMaterial 死字段清理** — `useAlbedoMap`/`useNormalMap`/`useMetallicMap`/`useRoughnessMap`/`useAOMap` 始终为 0 → 可移除
- [ ] **Compute Tile Light Culling** — 用 Compute Shader 预计算 per-tile light list

---

## 18. GDR 性能优化 (2026-06-20)

> **优化目标:** 消除 CPU 冗余剔除，引入跨帧缓存，接入 VSync，补全 Alpha Map 管线

### 18.1 盲提交 (Blind Submit) — CPU 视锥体剔除消除

**问题:** CPU 每帧花费 ~1.5ms 做视锥体剔除 + RenderQueue 排序，但 GPU 的 compute
shader (`cull.comp`) 已经做了一遍同样的剔除。双重视锥体剔除纯属浪费。

**方案:** `GDRContext::BuildFromScene()` 直接遍历 ECS
(`view<TransformComponent, MeshRendererComponent>`)，将所有 Opaque+Masked 实体
盲目上传到 SSBO。CPU 不再做任何视锥体剔除或深度排序。GPU compute shader 是唯一
的可见性仲裁者。

```cpp
// SceneRenderer::RenderScene() — 盲提交 + 仅半透明走 RenderQueue
s_GDRContext->BuildFromScene(scene.get(), vkCtx->GetGeometryPool(), frameIdx);
// RenderQueue 仅处理半透明 (bucket > 1), 用于 WBOIT 后向前排序
```

**关键改动:**
- `kMaxInstances` 4096 → 65536 (盲提交需要容纳场景全集)
- `m_LastBuiltFrame` 防护: 同一帧内 Game + Editor 两个视口只构建一次 SSBO
- GBuffer 空检查改用 `InstanceCount == 0` 替代 `RenderQueue.Packets.empty()`

**收益:** CPU cull 时间从 1.47ms → ~0.15ms (仅 ECS 遍历 + memcpy)

---

### 18.2 Transform 哈希缓存 — O(1) GetWorldTransform()

**问题:** `Entity::GetWorldTransform()` 每帧递归计算父链矩阵乘法，即使物体完全静止。

**方案:** 基于哈希的跨帧缓存，无需修改任何 write path (不需要 setter)。

```cpp
// TransformComponent 新增字段 (mutable, const 方法可修改)
mutable glm::mat4 CachedWorldMatrix  = glm::mat4(1.0f);
mutable uint64_t  LastLocalHash      = 0;
mutable uint64_t  LastParentWorldHash = 0;

uint64_t ComputeLocalHash() const;  // 混合 Translation+Rotation+Scale 的 9 个 float
```

```cpp
// Entity::GetWorldTransform() — 哈希缓存
uint64_t localHash = t.ComputeLocalHash();
uint64_t parentWorldHash = HashMat4(parentWorld);  // 父节点世界矩阵的哈希
if (localHash == t.LastLocalHash && parentWorldHash == t.LastParentWorldHash)
    return t.CachedWorldMatrix;  // 缓存命中 — O(1)
// 缓存未命中: 重算并更新
t.CachedWorldMatrix = parentWorld * t.GetTransform();
t.LastLocalHash = localHash;  t.LastParentWorldHash = parentWorldHash;
```

**关键修复:** 使用 `HashMat4(parentWorld)` (父节点**世界**矩阵哈希)，而非
`parent.LastLocalHash` (父节点本地哈希)。原始版本用父节点本地哈希，导致祖父节点
移动时子节点错误命中缓存 (Bug found & fixed 2026-06-20)。

**收益:** 静态物体零矩阵乘法开销，仅 9 个 float 哈希 + 2 个 uint64 比较。

---

### 18.3 O(1) 激活状态检查 — CachedActiveInHierarchy

**问题:** `Entity::IsActiveInHierarchy()` 每帧 O(depth) 递归父链遍历，每实体每帧。

**方案:** 自上而下传播的缓存布尔值。

```cpp
// RelationshipComponent 新增字段
bool CachedActiveInHierarchy = true;  // O(1) 只读访问

// Scene::PropagateActiveState() — 自上而下传播
void Scene::PropagateActiveState(Entity entity) {
    bool parentActive = /* 父节点缓存值 */;
    bool selfActive = entity.GetComponent<TagComponent>().IsActive;
    rel.CachedActiveInHierarchy = parentActive && selfActive;
    for (auto child : rel.Children) PropagateActiveState(child);  // 递归到子节点
}
```

**调用点:** `SetParent()` 后, `TagComponent::IsActive` 切换后,
场景反序列化完成后 (所有根实体)。每个调用仅影响子树，非全场景。

**收益:** `IsActiveInHierarchy()` 从 O(depth) → O(1)。

---

### 18.4 资产指针缓存 + 事件驱动作废

**问题:** `AssetManager::GetAsset<>()` 每实体每帧做哈希表查找。
`BuildFromScene()` 遍历 1000+ 实体时此开销显著。

**方案:** `MeshRendererComponent` 中缓存 `shared_ptr<Model/Material>`，
AssetWatcher 热重载时作废。

```cpp
// MeshRendererComponent 新增字段
mutable std::shared_ptr<class Model>    CachedModel    = nullptr;
mutable std::shared_ptr<class Material> CachedMaterial = nullptr;
```

```cpp
// Scene::InvalidateAssetCache() — 热重载时调用
void Scene::InvalidateAssetCache(UUID assetHandle) {
    auto view = m_Registry.view<MeshRendererComponent>();
    for (auto entityID : view) {
        auto& comp = view.get<MeshRendererComponent>(entityID);
        if (comp.MaterialHandle == assetHandle) comp.CachedMaterial = nullptr;
        if (comp.ModelHandle == assetHandle)    comp.CachedModel    = nullptr;
    }
}
```

```cpp
// AssetWatcher::Update() 现在返回重载的 UUID 列表
auto reloadedUUIDs = m_AssetWatcher.Update();
for (auto uuid : reloadedUUIDs)
    m_ActiveScene->InvalidateAssetCache(uuid);
```

**Bug 修复:** 初始版本缺少作废机制，导致场景热重载后 `BuildFromScene()` 通过
过期指针上传了已被释放的 bindless 纹理索引 (GPU 采样到无效 descriptor 槽位)，
引发 `VK_ERROR_DEVICE_LOST`。事件驱动作废彻底消除此问题。

**收益:** `AssetManager::GetAsset<>()` 哈希表查找 → `shared_ptr` 解引用。

---

### 18.5 u_AlphaMap GPU 管线接入

**问题:** Material 系统有 `u_AlphaMap` 纹理槽 (PropertiesPanel 可配置，
`.mat` 文件可序列化)，但 `BakeProperties()` 忽略它，`BakedPC` 无对应字段，
`GPUMaterial` 无 `alphaBindless`。Fragment shader 仅从 `albedo.a` 取 alpha。

**方案:** 全链路接入 alpha map。

| 层 | 改动 |
|----|------|
| `BakedPC` | 新增 `AlphaMapIndex = 1` (白色回退) |
| `BakeProperties()` | 处理 `u_AlphaMap` → 解析 bindless 索引 |
| `GPUMaterial` | 新增 `useAlphaMap` + `alphaBindless`，`_pad[4]` → `_pad[2]` (sizeof 保持 112) |
| `GDRContext` | `gm.alphaBindless = (int)b.AlphaMapIndex; gm.useAlphaMap = (AlphaMapIndex != 1) ? 1 : 0;` |
| `gbuffer_gdr_bindless.frag` | `if (mat.useAlphaMap != 0) a *= texture(..., mat.alphaBindless).r;` (在 discard 之前) |
| `shadow_gdr_masked.frag` | 同上 |
| `cull_shadow.comp`, `gbuffer_gdr.vert` | 更新 GPUMaterial struct 定义 |

**收益:** `u_AlphaMap` 纹理槽现在对 masked/alpha-test 材质生效。

---

### 18.6 VSync 接入 Vulkan Present Mode

**问题:** ImGui "Enable VSync" 复选框仅调用 `glfwSwapInterval()` — 对 Vulkan
swapchain 无效。`VulkanContext::ChooseSwapPresentMode()` 硬编码 `MAILBOX_KHR`。

**方案:**
- `VulkanContext::m_VSync` + `SetVSync(bool)`: VSync on→`FIFO_KHR`, off→`IMMEDIATE_KHR`→`MAILBOX_KHR`
- Swapchain 重建延迟到 `BeginFrame()` 安全点 (fence waited 之后)，避免 ImGui 渲染中途销毁 swapchain 导致崩溃
- `Window::SetVSync()` 对 Vulkan 后端分支到 `VulkanContext::SetVSync()`
- `PreferencesPanel` 启动时应用 YAML 中的 `EnableVSync` 值
- Semaphore 改用 `m_ImageIndex` (per-swapchain-image) 索引，swapchain 重建后不再残留旧关联

---

### 18.7 Vulkan 验证层修复

| 修复 | 说明 |
|------|------|
| Pipeline Topology (Bug H) | `Triangles` 显式映射为 `TRIANGLE_LIST`，不再依赖空顶点布局启发式 (导致 GDR 面撕裂) |
| Count Buffer INDIRECT_BIT | `vkCmdDrawIndexedIndirectCount` 的 count buffer 需要 `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT` |
| Push Constant stageFlags | VS-only pipeline 使用 `VK_SHADER_STAGE_VERTEX_BIT`；masked pipeline 使用 `VERTEX\|FRAGMENT` |
| `drawIndirectCount` | `VkPhysicalDeviceVulkan12Features::drawIndirectCount = VK_TRUE` |
| `NoFragmentShader` | `PipelineSpecification` 新增 flag — depth-only pass 跳过 fragment stage (硬件 Early-Z) |
| Depth Bias | `PipelineSpecification` 新增 `DepthBiasEnable/ConstantFactor/SlopeFactor/Clamp` 字段，接入 rasterizer |
| GeometryPool HOST_COHERENT | `requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT` |
| GPU-Assisted Validation | 条件性启用 `VK_EXT_validation_features` + `GPU_ASSISTED_EXT`，用于 shader 级故障检测 |

---

### 18.8 帧耗时打点 (已注释, 可重新启用)

`Application::Run()` 和 `SceneRenderer::RenderScene()` 中已埋入
`std::chrono::high_resolution_clock` 打点。取消 `if (++s_FrameIdx % 60 == 0)`
和 `if (++s_SceneFrameIdx % 60 == 0)` 的注释即可启用每 60 帧的性能日志输出。

**日志格式:**
```
[Frame 960] total=2.37ms | pre=0.02 beginFrame=0.09 onUpdate=1.60(GDR:0.047) imGui=0.49 endFrame=0.17
[Scene] gdr=1.400ms queue=0.010ms graph=0.070ms ubo=0.050ms exec=0.500ms gdrInst=80 qPackets=0
```

---

### 18.9 Lua GDR 性能测试脚本

**文件:** `assets/AyayaProject/testGPU-DrivenRendering/Assets/Scripts/GDRBenchmark.lua`

- 生成 N 个 prefab 实例 (teapot.prefab)，支持 Grid/Line/Circle/RandomSphere 布局
- 使用 `SetTranslationsBatch` + `SetRotationsBatch` Lua API 进行批量变换更新
- 键盘控制: Up/Down ±100, Left/Right ±10, W/S ±1, R=reset, G/L/C/X=layout, F=切换FPS
- 增量重建: count 变化时仅创建/销毁差值

---

## 19. MoltenVK/M1 兼容性 + Hi-Z 鲁棒性修复 (2026-06-23)

> **目标:** 修复 Mac M1 (MoltenVK) 上的 vkCreateDevice 崩溃、Hi-Z 误剔除、Shadow Pass 固定槽位适配

### 19.1 Feature-Driven 能力注册表

**文件:** `VulkanContext.hpp:18-22`, `VulkanContext.cpp:264-294`

新增 `VulkanCapabilities` 结构体，在设备创建时通过 `vkGetPhysicalDeviceFeatures2` 查询并缓存：

| 能力 | macOS (M1) | Windows (RTX3060) | 用途 |
|------|-----------|-------------------|------|
| `HasDrawIndirectCount` | false (runtime) | true (runtime) | Shadow Pass: atomic vs fixed-slot 路径 |
| `HasBindlessTextures` | true (runtime) | true (runtime) | Bindless 纹理数组 |
| `HasHardwarePCF` | false (`#ifdef __APPLE__`) | true | 阴影采样器: `sampler2DShadow` vs 手动 PCF |

### 19.2 Shadow Pass — 双路径 GPU-Driven 剔除

**文件:** `VulkanShadowPass.hpp`, `VulkanShadowPass.cpp`

- **Atomic 路径** (桌面): `atomicAdd` 压缩到 opaque/masked bin → `vkCmdDrawIndexedIndirectCount`
- **Fixed-Slot 路径** (MoltenVK/M1): 固定槽位写入，被剔除/非阴影投射者写 `instanceCount=0` → `vkCmdDrawIndexedIndirect` with `drawCount=InstanceCount`
- 排他性初始化 (`if/else`): 只创建当前路径需要的资源，不浪费 VRAM
- 连续 descriptor bindings (0,1): 修复 MoltenVK Metal argument buffer 的 sparse binding 问题

**Shader 变体:** `cull_shadow.comp` → `_atomic` / `_fixed` (via `#ifdef USE_INDIRECT_COUNT`)

### 19.3 阴影采样器 — 手动 PCF 回退

**文件:** `VulkanFramebuffer.cpp:246-259`, `deferred_lighting.frag`, `pbr_forward.frag`

- macOS: `compareEnable=VK_FALSE` (非比较采样器) + `sampler2D` + 手动深度比较
- Desktop: `compareEnable=VK_TRUE` + `sampler2DShadow` 硬件 PCF
- Shader 变体: `USE_HARDWARE_PCF` → default / `_nohwpc`

### 19.4 Shader 排列系统

**文件:** `compile_shaders.py`

新增 `SHADER_VARIANTS` 字典，支持 `glslc -DKEY=VALUE` 从单一 GLSL 源生成多个 SPIR-V：
- `cull_shadow.comp` → `_atomic` / `_fixed`
- `deferred_lighting.frag` → default / `_nohwpc`
- `pbr_forward.frag` → default / `_nohwpc`

### 19.5 Hi-Z 遮挡剔除 — 鲁棒性强化

**文件:** `cull_hiz.comp`, `hiz_downsample.comp`, `VulkanGbufferPass.cpp`

#### 19.5.1 解析解 AABB (cull_hiz.comp)

- 切线锥面与 Z=1 像平面相交的精确投影 → 真实的 NDC min/max 边界
- 正确处理透视变形（投影中心 ≠ 球心投影），替代所有 `radius/w` 近似
- 正确的 Vulkan NDC 符号：`clip.w = -View.z` → `NDC = -(X/Z) * Proj`（修复镜像错位）
- NDC→UV 半宽: `(max-min) * 0.25`（NDC span=2.0, UV span=1.0，修复双倍膨胀）

#### 19.5.2 近平面防护

- `nearestClip.w ≤ 0` → 球体穿透近平面，保守保留
- `instanceDepth ≤ 1e-4` → 小 w 除法导致数值不稳定，保守保留
- `w² ≤ r² 或 C.z² ≤ r²` → 相机在球体内，保守保留

#### 19.5.3 9-Tap 网格采样

- 中心 + 4 个十字边缘中点 + 4 个角落，覆盖整个投影 AABB
- `safeEdge = uvHalfSize * 0.9` — 10% 安全余量，防止透视精度越界
- 深度偏置: `0.01 + 0.01 * mip` — 补偿相机远离物体时的帧间深度差
- Mip: `floor(log2(rectSizePixels)) - 1` — 为保证 9 点空间分辨率降一级

#### 19.5.4 自适应边界降采样 (hiz_downsample.comp)

- 奇数分辨率修复：最后一个目标像素消费源 Mip 所有剩余像素
- 条件: `uv.x == dstSize.x - 1 → maxX = srcSize.x - 1`
- 动态循环: 普通 2×2，边缘 3×2/2×3/3×3

#### 19.5.5 Barrier 修复 (VulkanGbufferPass.cpp)

- `cull.comp → cull_hiz.comp` barrier: `dstAccess` 从 `INDIRECT_COMMAND_READ` 改为 `SHADER_READ | SHADER_WRITE`; `dstStage` 从 `DRAW_INDIRECT` 改为 `COMPUTE_SHADER`
- 8 处 `layerCount=0→1` (image views, barriers, clearColorImage) — Vulkan spec 要求 ≥1

### 19.6 其他修复

- `RenderGraph.cpp`: `InsertTileResolveBarrier` 深度路径 `srcStage` 增加 `TRANSFER_BIT` — 覆盖 MoltenVK TBDR tile→memory store
- `PreferencesPanel.cpp`: YAML float→int 转换加固（GraphicsAPI 为 float 时不再崩溃）
- `VulkanLightingPass.cpp` / `VulkanForwardTestPass.cpp`: macOS 加载 `_nohwpc` shader 变体

### 19.7 已知问题（待修复）

**极限情况:** 当画面前面的物体靠近相机最左边时，会触发后面物体的误剔除。

- **症状:** 前景遮挡物占据屏幕左侧区域，位于其右侧的后方物体被 Hi-Z 错误剔除
- **分析:** 遮挡物在粗 Mip 级别中向外"晕染"（MAX 降采样的固有特性），覆盖了后方物体 AABB 的整个 footprint 区域。即使 footprint 面积采样遍历所有 texel，若遮挡物填充了整个 footprint，后方物体仍会被剔除
- **方向:** 可能需要进一步提升 Mip 精度（增加降级 offset）或引入深度范围阈值

### 19.8 Hi-Z 完整 Shader 清单

| Shader | 阶段 | 功能 |
|--------|------|------|
| `cull.comp` | Compute | Gribb-Hartmann 6-平面视锥体剔除 |
| `cull_hiz.comp` | Compute | 时间性 Hi-Z 遮挡剔除 (上一帧深度) |
| `hiz_build.comp` | Compute | GBuffer D32→R32 Hi-Z level-0 拷贝 |
| `hiz_downsample.comp` | Compute | 2×2 MAX 降采样 Mip 链 (自适应边界) |
| `cull_shadow.comp` | Compute | 光源视锥体剔除 + Opaque/Masked 分 bin
