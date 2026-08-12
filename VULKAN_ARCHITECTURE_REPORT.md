# Ayaya Vulkan Rendering Engine — Architecture Report

> **Date:** 2026-08-07
> **Branch:** `feature/gpu-driven-bindless`
> **Backend:** Vulkan 1.3 with Dynamic Rendering
> **Platforms:** macOS (MoltenVK), Windows (Native)
> **Analysis Scope:** 14 subsystems — RenderGraph, SceneRenderer, GDR, all 16 passes, Vulkan backend, shaders (72+ files), SRP, IBL, WBOIT, SSAO, SSR (fully implemented), asset pipeline, editor

---

## Table of Contents

1. [Overview & Architecture Philosophy](#1-overview--architecture-philosophy)
2. [RenderGraph — DAG Frame Graph](#2-rendergraph--dag-frame-graph)
3. [SceneRenderer — Orchestration Layer](#3-scenerenderer--orchestration-layer)
4. [GPU-Driven Rendering (GDR)](#4-gpu-driven-rendering-gdr)
5. [Deferred Rendering Pipeline](#5-deferred-rendering-pipeline)
6. [Vulkan Backend Infrastructure](#6-vulkan-backend-infrastructure)
7. [Shader Architecture](#7-shader-architecture)
8. [SRP — Scriptable Render Pipeline](#8-srp--scriptable-render-pipeline)
9. [Image-Based Lighting (IBL)](#9-image-based-lighting-ibl)
10. [WBOIT — Order-Independent Transparency](#10-wboit--order-independent-transparency)
11. [Post-Processing Pipeline](#11-post-processing-pipeline)
12. [Asset System Integration](#12-asset-system-integration)
13. [Editor Integration](#13-editor-integration)
14. [Screen-Space Reflections (SSR)](#14-screen-space-reflections-ssr)
15. [Known Issues & Future Directions](#15-known-issues--future-directions)

---

## 1. Overview & Architecture Philosophy

### Dual-Backend Design

Ayaya supports both **OpenGL** and **Vulkan** backends through a factory pattern. Every graphics resource (`Shader`, `Texture2D`, `Framebuffer`, `Pipeline`, `RenderCommandBuffer`) uses a static `Create()` factory that dispatches to the correct platform implementation based on `RendererAPI::GetAPI()`. The OpenGL backend uses a linear `RenderPipeline` executor; the Vulkan backend uses the DAG-based `RenderGraph`.

### Key Design Principles

1. **GPU-Driven Rendering (GDR):** All geometry data lives in a single monolithic `GlobalGeometryPool` (512 MB). Per-frame, SSBOs containing instances, geometry ranges, and materials are uploaded. Compute shaders perform frustum culling on the GPU, writing indirect draw commands. The CPU never iterates individual draw calls in the hot path.

2. **Bindless Textures:** All textures register into a single large descriptor set (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`). Shaders index into the array with flat integer IDs, eliminating per-material descriptor set churn.

3. **Scriptable Render Pipeline (SRP):** The entire pass DAG is defined in Lua `.srp` scripts, with all parameters baked into pure C++ structs — zero Lua calls in the hot path.

4. **Dynamic Rendering:** The entire Vulkan backend uses `vkCmdBeginRendering`/`vkCmdEndRendering` (Vulkan 1.3) instead of `VkRenderPass`/`VkFramebuffer` objects.

5. **Triple-Buffering:** 3 frames in flight, synchronized across `VulkanContext` (`m_FramesInFlight = 3`), `VulkanUniformBuffer` (`m_FramesInFlight = 3`), and `RenderGraph` (`kRenderGraphFramesInFlight = 3`).

### Component Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        SceneRenderer                             │
│  ┌──────────┐  ┌──────────┐  ┌────────────┐  ┌──────────────┐  │
│  │ GDRContext│  │RenderGraph│  │PipelineBldr│  │14 Pass Inst. │  │
│  └──────────┘  └──────────┘  └────────────┘  └──────────────┘  │
│       │              │               │               │           │
└───────┼──────────────┼───────────────┼───────────────┼───────────┘
        │              │               │               │
   ┌────▼────┐   ┌─────▼──────┐  ┌────▼─────┐   ┌────▼──────────┐
   │SSBO Data│   │DAG + FBOs  │  │Lua→C++   │   │Shadow/GBuffer │
   │Hub      │   │+ Barriers  │  │Baking    │   │Lighting/...   │
   └─────────┘   └────────────┘  └──────────┘   └───────────────┘
```

---

## 2. RenderGraph — DAG Frame Graph

The `RenderGraph` is the central scheduling system for the Vulkan backend. It replaces the linear pass list used by OpenGL.

### Core Data Structures

#### RGTexture

A named render target with triple-buffered physical `VulkanFramebuffer` instances:

| Field | Purpose |
|-------|---------|
| `Name` | Unique key in `m_Textures` map |
| `Spec` | `FramebufferSpecification` (resolution, formats, samples) |
| `PhysicalFBOs[3]` | Triple-buffered FBO instances, one per frame-in-flight |
| `IsWritten` / `IsRead` | Dependency tracking flags |
| `CurrentLayout[3]` | Per-frame color attachment layout tracking |
| `DepthLayout[3]` | Per-frame depth attachment layout tracking (independent from color) |

**Key design:** Color and depth layouts are tracked **separately** because a mixed FBO (color+depth) can have color in `SHADER_READ_ONLY` while depth remains in `ATTACHMENT_OPTIMAL`. `HasDepthAttachment()` scans attachment specs for Depth or `DEPTH24STENCIL8` formats.

#### RGPass

A single node in the DAG:

| Field | Purpose |
|-------|---------|
| `Name` | Unique pass identifier |
| `PassType` | Factory name (e.g., `"SSAOPass"`) for type-based culling |
| `ExecuteCallback` | `RGExecuteFn` — per-frame lambda invoked during `Execute()` |
| `TextureReads` | `vector<string>` — names of textures this pass samples |
| `TextureWrites` | `vector<string>` — names of textures this pass renders to |
| `WriteLoadOps` | `unordered_map<string, AttachmentLoadOp>` — per-output load operation |
| `DepthReadTextures` | `unordered_set<string>` — textures read as depth attachments (keep ATTACHMENT layout) |
| `HasSideEffect` | Force execution even with no writes (UI pass, compute passes) |
| `IsCulled` | Per-frame cull flag — culled passes are skipped but preserved |

#### RGBuilder (DSL)

A builder object passed to the setup lambda during `AddPass`:

- **`ReadTexture(name)`** — Mark `name` as a shader-sampled input. Sets `tex->IsRead = true`.
- **`ReadTextureAsDepth(name)`** — Declare depth attachment read. The texture is NOT transitioned to `SHADER_READ_ONLY` — stays in `ATTACHMENT_OPTIMAL`.
- **`WriteTexture(name, spec, loadOp=Clear)`** — Declare exclusive write. One texture can only be written by one pass.
- **`ReadWriteTexture(name, spec, loadOp=Load)`** — Declare read+write. Creates both a read edge (dependency on previous writer) and a write edge (chains with next writer).
- **`SetCulled(bool)`** / **`IsCulled()`** — Per-frame enable/disable.

### DAG Construction (Compile)

**Step 1 — Culling:**
Culled passes (`IsCulled == true`) are excluded from the DAG but preserved at the back of `m_Passes` for re-enabling. If all passes are culled, the graph clears completely.

**Step 2 — Producer Mapping (single-pass, version-aware):**
Passes are iterated in **declaration order** (not topological order). For each pass:

1. **Read edges:** For each texture read (excluding those also written by this pass), look up the **current** producer. If a producer exists and is not this pass, add a `(producer, this_pass)` read edge.
2. **Write edges:** For each texture written, if a prior producer exists, add an implicit serialization edge `(prior_producer, this_pass)`. Then update the producer map to `this_pass`.

This single-pass approach correctly handles "version tracking" — a pass reading `"Lighting"` at position N sees `LightingPass` (not a later `WBOIT_Resolve` at position N+3). The producer map reflects the pipeline state at that exact point in declaration order.

**Step 3 — Kahn Topological Sort:**
Standard BFS-based: enqueue zero-in-degree passes, dequeue, decrement neighbor in-degrees. If `sorted.size() != active.size()`, logs an error with circular dependency detection and appends unreachable passes to the end (best-effort).

**Step 4 — FBO Creation:**
For each texture with `IsWritten == true`, creates 3 `VulkanFramebuffer` instances (one per frame-in-flight). Layout tracking initialized once:
- **Depth-only textures:** `DepthStencilAttachmentOptimal`
- **Other textures:** `ShaderReadOnlyOptimal` (for ImGui compatibility)
- **Mixed color+depth:** color = `ShaderReadOnlyOptimal`, depth = `DepthStencilAttachmentOptimal`

### Triple-Buffered FBOs

Each `RGTexture` owns `PhysicalFBOs[3]` — three independent `VulkanFramebuffer` instances. `Execute()` uses `vkCtx->GetCurrentFrameIndex() % 3` to select the FBO for the current frame, preventing GPU write-after-read hazards across frames. Layout tracking (`CurrentLayout[i]`, `DepthLayout[i]`) is also triple-buffered.

### Barrier Management

Three key methods handle automatic layout transitions between passes:

| Method | When | What |
|--------|------|------|
| `EnsureReadable` | Before pass reads TextureReads (excluding DepthReadTextures) | Transitions color attachments to `SHADER_READ_ONLY_OPTIMAL`; transitions depth attachment from `UNDEFINED`/`DEPTH_STENCIL_ATTACHMENT` to `DEPTH_STENCIL_READ_ONLY` |
| `EnsureWritable` | Before pass writes + before DepthReadTextures | Transitions to `COLOR_ATTACHMENT_OPTIMAL` / `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` |
| `InsertTileResolveBarrier` | After pass writes + after DepthReadTextures | Transitions back to `SHADER_READ_ONLY_OPTIMAL` / `DEPTH_STENCIL_READ_ONLY_OPTIMAL` + TBDR tile cache flush |

**Execute order per pass (in topological order):**
```
1. EnsureReadable  for TextureReads (excluding DepthReadTextures)
2. EnsureWritable  for TextureWrites
3. EnsureWritable  for DepthReadTextures  // keep ATTACHMENT layout
4. Inject FBOs into context.Framebuffers (writes override, reads only if not present)
5. ExecuteCallback(ctx, cmd) — with CPU+GPU timing
6. InsertTileResolveBarrier for TextureWrites
7. InsertTileResolveBarrier for DepthReadTextures
```

Step 1 runs BEFORE Step 2: for `ReadWriteTexture` targets, calling `EnsureReadable` after `EnsureWritable` would undo the attachment transition. By running reads first, the write transition takes precedence.

### StateSnapshot — Atomic SRP Rebuild

```cpp
struct StateSnapshot {
    std::vector<std::shared_ptr<RGPass>> Passes;
    std::unordered_map<std::string, RGTexture> Textures;
    bool Compiled = false;
};
```

`ExtractState()` move-steals passes and textures into a snapshot, leaving the graph cleared. This is called BEFORE SRP Lua compilation. If Lua fails, `RestoreState()` reinstates the previous valid graph — no black frame on script errors. FBOs survive in the snapshot because `RGTexture` holds `shared_ptr<Framebuffer>` arrays.

### Pass Culling

`ApplyPerFrameCulling()` toggles `RGPass::IsCulled` per frame based on scene settings read from `RenderContext`:
- `SSAOPass` → `EnableSSAO`
- `SSRPass` / `SSRBlurPass` → `EnableSSR` (ApplyReflection never culled)
- `BloomPass` → `EnableBloom`
- `OutlinePass` → `EnableOutline`
- `WBOIT_Gather` / `WBOIT_Resolve` → translucent packets in queue
- **FXAA is never culled** (uses push-constant toggle for passthrough)

Matching uses `PassType` (factory name) first, falling back to `Name`. Culled passes are preserved in the pass list — toggling at runtime doesn't lose the pass definition.

### PassRegistry

Global factory registry mapping pass type names to `IPassFactory` instances:

```cpp
class IPassFactory {
    virtual void DeclareResources(RGBuilder&, uint32_t w, uint32_t h, const PassBakedParams&) = 0;
    virtual RGExecuteFn GetExecuteFn() = 0;
};
```

**17 registered passes in `PassRegistry::Init()`:**
14 built-in + `GenericDrawPass`, `GenericFullScreenPass`, `GenericComputePass`.

**Factory variants:**
- `StandardPassFactory<TPass, DeclareFn>` — template for standard passes. `DeclareFn` can be `void(RGBuilder&)` (fixed-size) or `void(RGBuilder&, uint32_t, uint32_t)` (viewport-dependent).
- `WBOITGatherFactory` / `WBOITResolveFactory` — special-cased for two-pass WBOIT
- `LightingPassFactory` — adds soft `builder.ReadTexture("SSAO_Final")` dependency
- Generic pass factories — `DeclareResources` is a no-op (resources declared by SRP Lua)

---

## 3. SceneRenderer — Orchestration Layer

### Ownership

`SceneRenderer` holds all rendering state for a single viewport. The editor uses two independent instances (`m_SceneRenderer` for editor viewport, `m_GameRenderer` for game viewport).

### Key Members

| Member | Type | Purpose |
|--------|------|---------|
| `m_RenderGraph` | `RenderGraph` | Vulkan DAG frame graph |
| `m_Pipeline` | `RenderPipeline` | OpenGL linear pass executor |
| `m_GDRContext` | `shared_ptr<GDRContext>` | GPU-driven rendering data hub (shared across multiple SceneRenderers) |
| `m_PipelineBuilder` | `unique_ptr<PipelineBuilder>` | SRP Lua→RenderGraph bridge |
| `m_RenderContext` | `RenderContext` | Data blackboard shared across all passes |
| `m_RenderQueue` | `RenderQueue` | Sorted draw packets |
| `m_CameraUniformBuffer` | `shared_ptr<UniformBuffer>` | Camera UBO (176 bytes, Set 0 Binding 0) |
| `m_DirLightUniformBuffer` | `shared_ptr<UniformBuffer>` | Directional light UBO (Set 0 Binding 1) |
| `m_FinalExportTexture` | `string` | Name of the final output texture (default `"FXAA"`) |
| `m_ViewportDirty` | `bool` | Triggers full RenderGraph rebuild on next frame |
| `m_SRPScriptHandle` | `UUID` | 0 = hardcoded pipeline; non-zero = Lua SRP script |
| `m_SRPDirty` | `bool` | Triggers Lua re-execution on next graph build |

### 14 Shared Pass Instances

All `shared_ptr<RenderPass>`:
`m_ShadowPass`, `m_GBufferPass`, `m_DepthPass`, `m_SSAOPass`, `m_SSRPass`, `m_SSRBlurPass`, `m_ApplyReflectionPass`, `m_LightingPass`, `m_ForwardBlendPass`, `m_WBOITPass`, `m_OutlinePass`, `m_BloomPass`, `m_PostProcessPass`, `m_FXAAPass`, `m_UIPass`

### SSBOs for Lighting

| SSBO | Max Count | Struct Size | Content |
|------|-----------|-------------|---------|
| `m_PointLightSSBO` | 65,536 | 32 bytes | `{position+radius, color+falloff}` per light + header `{count, 3*pad}` |
| `m_LightInstanceSSBO` | 65,536 | `mat4` per instance | Light volume sphere instance transforms |
| `m_SpotLightSSBO` | 256 | 64 bytes | `{position+radius, color+falloff, direction+coneAngles, outerCone+pad}` |
| `m_SpotLightInstanceSSBO` | 256 | `mat4` per instance | Spot light volume transforms |

### RenderScene() — Main Render Entry Point

```
1. Clear context state — zero Stats, FrameSteps, PassProfiles, Framebuffers
2. Camera exposure — find Primary camera, compute physicalExposure = 1/(2^EV100 * 1.2)
3. Directional light — first active DirectionalLightComponent, compute direction from quaternion
4. Point lights (SSBO) — frustum-cull each light via AABB, convert lumens→candelas (cd=lm/4π)
5. Spot lights (SSBO) — same pattern, compute cone direction from entity rotation
6. Upload DirLight UBO via m_DirLightUniformBuffer->SetData()
7. GDR Build — m_GDRContext->BuildFromRenderQueue() rebuilds all 3 SSBOs
8. RenderQueue — collect translucent packets, build SortKeys, sort, inject pointer into context
9. Context population — viewport, IBL, clear color, editor flags (skybox/grid/outline)
10. PostProcessVolume — read SSAO/SSR/Bloom/FXAA settings, inject into context
11. SRP global shader params — inject baked globals from PipelineBuilder
12. Graph execution (Vulkan) — BuildRenderGraph → m_RenderGraph.Execute(m_RenderContext, *cmd)
13. Graph execution (OpenGL) — m_Pipeline.Execute(m_RenderContext, *cmd)
14. Stats collection — draw calls, shader binds, triangles, GDR diagnostics, GPU time
```

### RenderContext — Data Blackboard

All maps use `std::string` keys (not `string_view` — prevents dangling references when RenderGraph rebuilds):

| Map | Type | Purpose |
|-----|------|---------|
| `Framebuffers` | `unordered_map<string, shared_ptr<Framebuffer>>` | Named FBOs injected by RenderGraph |
| `Textures` | `unordered_map<string, shared_ptr<Texture2D>>` | Named 2D textures |
| `Settings` | `unordered_map<string, any>` | Type-erased key-value store (SRP globals, light SSBO handles) |
| `PassProfiles` | `unordered_map<string, PassProfileData>` | Per-pass CPU/GPU time, draw calls, triangle count |
| `FrameSteps` | `vector<DrawCallStep>` | Recorded draw call steps for frame debugger |
| `DebugStepLimit` | `int` (default -1) | When >=0, limits draws for single-stepping |

**Global prefix convention for SRP:** Passes access cross-pass constants via `context.Get<float>("Global.MyParam", default)`.

### Default RenderGraph DAG Construction

In `BuildRenderGraph_Default()`, when `m_ViewportDirty` is true:
1. `m_RenderGraph.Clear()` — removes all passes and textures
2. Adds 15 passes in declaration order:

| # | Pass | Writes | Reads |
|---|------|--------|-------|
| 1 | ShadowPass | `"ShadowMap"` (4096² Depth) | — |
| 2 | DepthPrePass | `"SceneDepth"` (R8+Depth) | — |
| 3 | GBufferPass | `"GBuffer"` (4×MRT+Depth) | — |
| 4 | SSAOPass | `"SSAO_Final"` (½res R8) | `"GBuffer"`, `"SceneDepth"` |
| 5 | SSRPass | `"SSR_Result"` (½res RGBA16F) | `"SceneDepth"`, `"GBuffer"`, `"Lighting"` |
| 6 | LightingPass | `"Lighting"` (RGBA16F+Depth) | `"GBuffer"`, `"SceneDepth"`, `"ShadowMap"`, `"SSAO_Final"` |
| 7 | SSRCompositePass | `"Lighting"` (LOAD) | `"SSR_Result"`, `"GBuffer"` |
| 8 | ForwardBlend | `"Lighting"` (LOAD) | — |
| 9 | WBOIT_Gather | `"WBOIT_Gather"` (RGBA16F+RG16F) | `"GBuffer"` |
| 10 | WBOIT_Resolve | `"Lighting"` (LOAD) | `"WBOIT_Gather"` |
| 11 | OutlinePass | `"Selection"` (RGBA8) | `"GBuffer"` |
| 12 | BloomPass | `"Bloom"` (½res RGBA16F) | `"Lighting"` |
| 13 | PostProcessPass | `"FinalOutput"` (RGBA8) | `"Lighting"`, `"Selection"`, `"Bloom"` |
| 14 | FXAAPass | `"FXAA"` (RGBA8) | `"FinalOutput"` |
| 15 | UIPass | `"FXAA"` (LOAD) | — |

`m_FinalExportTexture = "FXAA"`. Parenthesized passes are conditionally culled.

### RenderPipeline (OpenGL)

`RenderPipeline` is the older linear pass executor for the OpenGL backend. Passes are added in order via `AddPass()` and executed sequentially. Includes OpenGL-specific GPU timer queries (`GL_TIME_ELAPSED`) and per-pass profiling.

---

## 4. GPU-Driven Rendering (GDR)

### Architecture

The GDR system eliminates per-draw CPU iteration. All draw call data flows through GPU-resident SSBOs, with compute shaders performing frustum culling and generating indirect draw commands.

### GDRContext — Data Hub

Central data hub owned by `SceneRenderer`, shared across Shadow, GBuffer, DepthPrePass, and GenericDraw passes.

#### SSBOs (Set 2, triple-buffered per frame-in-flight)

| Binding | Buffer | Content | Stages |
|---------|--------|---------|--------|
| 0 | `InstanceSSBO` | `GPUInstance[kMaxInstances=65536]` | Vertex, Fragment, Compute |
| 1 | `GeometryRangeSSBO` | `GeometryRange[kMaxMeshes=1024]` | Vertex, Compute |
| 2 | `MaterialSSBO` | `GPUMaterial[kMaxMaterials=512]` | Vertex, Fragment, Compute |
| 3 | `GeometryPool` | Unified vertex/index buffer (StructuredBuffer<uint>) | Vertex |

The VkBuffer handles are pre-bound at `Init()` time — buffers are persistent-mapped, so handles never change. `BindSet2()` is a convenience method for `vkCmdBindDescriptorSets(cmd, ..., 2, 1, &Set2Descriptors[frameIndex], ...)`.

#### GPUInstance (96 bytes, alignas(16))

```
mat4 transform          // 64B — World transform matrix
vec4 boundingSphere     // 16B — xyz=world-space center, w=world-space radius (max-axis-scaled)
uint geometryRangeIdx   // 4B  — Index into GeometryRangeSSBO[]
uint materialIdx        // 4B  — Index into MaterialSSBO[]
uint entityId           // 4B  — Raw entt::entity ID (truncated to 16 bits)
uint flags              // 4B  — Bit 0=CastShadows, Bit 1=ReceiveShadows
```

#### GeometryRange (16 bytes, each maps to one sub-mesh)

```
uint vertexOffset       // uint-element offset into SSBO (byteOffset / 4) for StructuredBuffer indexing
uint indexOffset        // Byte offset for vkCmdBindIndexBuffer / firstIndex
uint vertexCount
uint indexCount
```

#### GPUMaterial (176 bytes, alignas(16))

```
vec4 albedo             // Base color tint (16B)
float metallic, roughness, ao, alpha
int useAlbedoMap, useNormalMap, useORMMap
int useMetallicMap, useRoughnessMap, useAOMap
int albedoBindless, normalBindless, ormBindless
int metallicBindless, roughnessBindless, aoBindless
float alphaCutoff
int blendMode           // 0=Opaque, 1=Masked, 3=Translucent
int useAlphaMap, alphaBindless
uint lightModeMask      // Per-material SRP pass routing bitmask
uint packing            // TexturePacking: 0=UE4_ORM, 1=glTF_MetalRough, 2=Separate
uint _pad[2]            // Padding to fill gap before customData
float customData[16]    // 64-byte TA-extensible field at offset 112
```

### BuildFromRenderQueue()

Called once per frame. Guarded by frame number monotonic check (`m_LastBuiltFrameNumber == frameNumber` skips redundant rebuilds):

1. **Pre-upload (warming):** Iterates all queue packets, calls `geoPool.GetOrUploadMesh(pkt.MeshAsset.get())`. O(1) hash lookup — upload only on first encounter.

2. **Deduplication loops:**
   - **Mesh dedup:** `meshToRanges` hash map of `Mesh*` → `uint32_t` range index. `GetOrUploadMesh()` returns a `GeometryRange`; first-time mesh pushes to `gdrRanges`; subsequent references reuse the same index.
   - **Material dedup:** `m_MaterialToIndex` hash map of `const Material*` → `uint32_t`. Pointer-based dedup, zero collision. First-time material populates `GPUMaterial` from `Material::GetBakedPC()` (pre-baked bindless indices, scalar values, packing enum).

3. **GPUInstance construction** per packet: transform, bounding sphere (world-space center + max-axis-scaled radius from matrix column lengths), geometry range index, material index, entity ID, flags.

4. **SSBO upload:** `memcpy` into triple-buffered persistent-mapped `VulkanStorageBuffer`. Capped to max limits. Debug stats (RangeCount, MaterialCount, TotalTriangles, geometry pool usage %) computed.

### Compute Culling (`cull.comp`)

**Pipeline layout (4 descriptor sets + push constants):**

| Set | Layout | Content |
|-----|--------|---------|
| 0 | Empty (dummy) | Placeholder for MoltenVK compat |
| 1 | Empty (dummy) | Placeholder for MoltenVK compat |
| 2 | GDR Set 2 | InstanceSSBO + RangeSSBO + MaterialSSBO + GeometryPool |
| 3 | Per-pass Set 3 | Indirect draw buffer output (`VkDrawIndexedIndirectCommand[]`) |

**Push constants (128 bytes):**
```cpp
struct FrustumPush {
    vec4 planes[6];       // 96B — Gribb-Hartmann frustum planes
    uint count;           // 4B  — InstanceCount
    uint lightModeMask;   // 4B  — Pass bitmask for material filtering
    uint overrideInstanceID; // 4B — 0xFFFFFFFF = use SV_InstanceID
    uint _pad;            // 4B
    vec4 texelSize;       // 16B — (1/tw, 1/th, tw, th)
};
```

**Dispatch flow:**
1. Host write barrier (`HOST → COMPUTE|VERTEX|FRAGMENT|INDEX`) — ensures CPU SSBO writes visible to GPU
2. Bind compute pipeline, Set 2 (GDR data), Set 3 (indirect buffer output)
3. Push frustum constants with `lightModeMask` from context
4. Dispatch `ceil(instanceCount / 64)` thread groups (64 threads per group)
5. Compute-to-indirect barrier (`COMPUTE → DRAW_INDIRECT`) — critical for MoltenVK/Apple Silicon
6. `vkCmdDrawIndexedIndirect` — single draw consuming all compute-written commands

**Empty scene safety:** When `instanceCount == 0`, all three triple-buffer indirect draw slots are zero-filled with `vkCmdFillBuffer` — critical for Apple Silicon TBDR correctness (prevents stale triple-buffer data from resurrecting).

### GlobalGeometryPool

A single monolithic `VkBuffer` (512 MB) holding ALL vertex and index data for every mesh.

- **Usage flags:** `VERTEX_BUFFER_BIT | INDEX_BUFFER_BIT | STORAGE_BUFFER_BIT | TRANSFER_DST_BIT`
- **Allocation:** VMA with `HOST_ACCESS_SEQUENTIAL_WRITE_BIT` + `HOST_COHERENT_BIT`
- **Growth pattern:** `m_Cursor` tracks current byte offset; data grows linearly, never freed
- **Vertex stride:** 11 uints (44 bytes) = `{pos.xyz(3), normal.xyz(3), uv.xy(2), tangent.xyz(3)}`
- **`GetOrUploadMesh(Mesh*)`:** O(1) hash lookup in `m_MeshRanges` → cached `GeometryRange` return, or upload vertices+indices via `memcpy` + return new range. `vertexOffset` is converted to uint-element units (divided by 4) for SSBO compatibility.
- **GPU-side interpretation:** `GetAyayaVertex(vertexID, instanceID)` in `AyayaGDR.hlsl` reads `StructuredBuffer<uint>` and unpacks via `asfloat()`.

### VulkanStorageBuffer

Triple-buffered persistent-mapped SSBO for GPU-readable storage buffers. Used for GDRContext SSBOs, PointLightSSBO, LightInstanceSSBO, and WBOITPass instance buffers.

```cpp
void SetData(const void* data, uint32_t size) {
    uint32_t fi = context->GetCurrentFrameIndex() % m_FramesInFlight;
    memcpy(m_AllocInfos[fi].pMappedData, data, size);  // Zero Vulkan API overhead
}
```

- Allocated with `VMA_ALLOCATION_CREATE_MAPPED_BIT` — persistent mapping
- `HOST_COHERENT_BIT` — CPU writes immediately visible to GPU without explicit flushing
- Each frame index writes to its own buffer (no GPU/CPU race)

### VulkanUniformBuffer

Same triple-buffered persistent-mapped pattern. `SetData()` writes current frame only. `SetDataAllFrames()` writes to all 3 frames (for one-shot AssetPreviewer thumbnail rendering). Each instance registers its frame buffers with `VulkanPipeline::SetGlobalUniformBuffer(binding, frameIndex, buffer, size)` for Set 0 binding.

### Hi-Z Occlusion Culling

**Status: FULLY BUILT BUT DISABLED.** Phase 1 and Phase 2 dispatch calls are gated behind `if (false && ...)`. The depth pyramid IS still built every frame for future use.

**Triple-buffered per frame-in-flight resources:** `HiZFrameResources` with `VkImage` (R32F), mip levels = `floor(log2(max(vpW, vpH))) + 1`, per-mip image views + sampler.

**Build pipeline:**
1. **`hiz_build.comp`:** Copy GBuffer depth attachment → Hi-Z mip 0 (R32F)
2. **`hiz_downsample.comp`:** MAX reduction chain mip 1→N-1. Per-mip pipeline barriers (`COMPUTE→COMPUTE`). Pre-created descriptor sets per mip per frame-in-flight.

**Culling (disabled):**
- **Phase 1 (`cull_hiz.comp`):** Previous frame's Hi-Z. Analytic tangent-cone sphere projection for conservative occlusion.
- **Phase 2 (`cull_hiz_phase2.comp`):** Current frame's Hi-Z. Full texel traversal of AABB footprint for false-positive reduction. Recovers temporally-occluded instances.

---

## 5. Deferred Rendering Pipeline

### Default Pass Order

```
Shadow → DepthPrePass → GBuffer → (SSAO) → (SSR) → Lighting →
  SSR → SSRBlur → ApplyReflection → ForwardBlend → (WBOIT_Gather → WBOIT_Resolve) →
  Outline → Bloom → PostProcess → FXAA → UI
```

### 5.1 Shadow Pass (`VulkanShadowPass`)

- **Writes:** `"ShadowMap"` — 4096×4096 depth-only, `IsShadowMap = true`
- **Early-out:** Skips entire pass if no active directional light (all hidden via visibility toggle)

**GDR compute culling — three-variant SPIR-V:**

| Variant | Technique | GPU Support |
|---------|-----------|-------------|
| `cull_shadow_atomic.comp` | `atomicAdd` on count SSBO + `vkCmdDrawIndexedIndirectCount` | Desktop (HasDrawIndirectCount) |
| `cull_shadow_fixed.comp` | Fixed-slot writes, culled instances get `instanceCount=0` | MoltenVK fallback |

**Push constants:** `FrustumPush` (112B: 6 light-space planes + count + lightModeMask)

**Two graphics pipelines:**
- **Opaque:** `shadow_gdr_opaque.vert` only (`NoFragmentShader=true`), `CullMode::Back`, hardware Early-Z. SSBO vertex pulling — position only (3 uints from geometry pool).
- **Masked:** `shadow_gdr_masked.vert` + `.frag`, `CullMode::None` (double-sided foliage). Pulls position+UV, alpha-test discard in fragment shader via bindless textures.

**Depth bias:** Constant=0.8, Slope=0.5, Clamp=0.001
**MoltenVK compat:** Dummy descriptor set layouts for Sets 0/1 in compute pipelines (MoltenVK requires valid handles)

### 5.2 Depth Pre-Pass (`VulkanDepthPrePass`)

- **Writes:** `"SceneDepth"` — R8 dummy color + Depth. The R8 dummy exists because the fragment shader needs a color attachment even with `ColorWrite = false`.
- **Pipeline:** `gbuffer_gdr.vert` (SSBO vertex pulling) + `depth_only.frag` (minimal, only alpha-test discard for masked materials, writes dummy)
- **`ColorWrite = false`, `DepthWrite = true`, `DepthOperator = Less`, `BackfaceCulling = Back`**
- **Compute culling:** Same pattern as GBuffer — own Set 3 indirect buffer. Always clear=true for depth.
- **Purpose:** Early-Z rejection for GBuffer and SSAO, reducing fragment shader invocations

### 5.3 GBuffer Pass (`VulkanGBufferPass`)

- **Writes:** `"GBuffer"` — 4 MRTs + Depth:

| Attachment | Format | Content |
|-----------|--------|---------|
| 0 | RG16F | Octahedral-encoded world-space normals |
| 1 | RGBA8 | Albedo RGB + 1.0 |
| 2 | RGBA8 | Metallic (R), Roughness (G), AO (B), unused |
| 3 | RGBA8 | CustomData (ReceiveShadows flag, etc.) |
| Depth | D24S8 | Scene depth — LOAD from DepthPrePass (clear=false), not CLEAR |

- **Vertex shader:** `gbuffer_gdr.vert` — SSBO vertex pulling via `GetAyayaVertex()`, outputs `v_FragPos`, `v_Normal`, `v_TexCoord`, `v_MaterialIdx` (flat), `v_Flags` (flat)
- **Fragment shader:** `gbuffer_gdr_bindless.frag` — bindless texture array for PBR maps, Material SSBO reading, full TexturePacking support (UE4_ORM, glTF_MetalRough, Separate)
- **Compute culling:** `cull.comp` with frustum planes + LightMode mask. Empty scene → `vkCmdFillBuffer` on all 3 indirect draw slots.
- **Hi-Z build:** Depth pyramid constructed after GBuffer (always built, culling disabled)

### 5.4 SSAO Pass (`VulkanSSAOPass`)

- **Writes:** `"SSAO_Final"` — half-res R8
- **Three sub-passes** (internal FBOs NOT RenderGraph-managed — manually transitioned):

1. **Generate (`ssao_generate.frag`):** 64-sample hemisphere sampling via 4×4 noise texture + TBN from GBuffer normal. Depth range check with smoothstep falloff. Power function post-process. Output to internal `m_RawFBO` (R8).
2. **Blur X (`ssao_blur.frag`):** 9-tap bilateral blur (cross-bilateral on depth+normal), horizontal. Spatial weight: `exp(-dist^2 * depthThresh)`. Normal weight: `pow(max(dot(n1,n2),0), 4.0)`. Output to internal `m_BlurXFBO` (R8).
3. **Blur Y:** Same kernel, vertical → writes to RenderGraph-managed `"SSAO_Final"` (R8).

- Conditionally culled when `EnableSSAO == false`
- Noise texture (`s_NoiseTexture`): 4×4 RGBA8, shared across all instances, released via `ReleaseNoiseTexture()`

### 5.5 Lighting Pass (`VulkanLightingPass`)

- **Writes:** `"Lighting"` — RGBA16F + Depth (CLEAR to black)

**Three phases in a single render pass:**

**Phase 1 — Directional + Ambient + IBL (fullscreen triangle):**
- Deferred pipeline (triangle strip, depth test ON, no blend, no cull)
- Binds 11 textures: `u_DepthMap` (0), `g_Albedo` (1), `g_PBR` (2), `g_CustomData` (3), `g_Normal` (4), `u_ShadowMap` (5), `u_IrradianceMap` (8), `u_PrefilteredMap` (9), `u_BRDFLUT` (10), `u_SSAO` (11, conditional)
- World position reconstruction from depth + inverse VP
- Octahedral normal decode from 2-channel RG16F
- Cook-Torrance BRDF: D_GGX, G_Smith, F_Schlick, F_SchlickR (roughness-aware fresnel)
- Shadow: 3×3 PCF (hardware via `sampler2DShadow` or manual via `sampler2D` on MoltenVK). Bias: `max(0.0003*(1-NdotL), 0.0001)`. Vulkan Y-flip compensation.
- IBL: Split-Sum Approximation — irradiance (diffuse) + prefiltered at `roughness*4.0` LOD + BRDF LUT
- SSAO: sampled from `u_SSAO` when `EnableSSAO==1`, modulates ambient term
- Point light SSBO (Set 2, Binding 0): `u_PointLightCount` + `u_PointLights[]`
- 6 debug mode views: normal, depth, normal, albedo, PBR, worldPos, depth compare, ambientOnly, directOnly, iblOnly

**Phase 2 — Point Light Volumes (instanced spheres, additive blend):**
- `m_LightVolumePipeline`: `CullMode::Front` (renders back faces — visible when camera enters sphere), `DepthTest OFF` (fragment shader does distance check)
- Set 2: 4 SSBOs — InstanceSSBO (vertex), PointLightSSBO (fragment), GeometryPool (vertex), SphereRangeBuffer (vertex)
- Instanced draw: `vkCmdDrawIndexed(sphereIndexCount, lightCount, 0, 0, 0)`
- Per-light PBR with inverse-square attenuation + UE4 windowing falloff

**Phase 3 — Spot Light Volumes (same pattern with cone attenuation):**
- `m_SpotLightVolumePipeline` with `light_volume_spot.frag`
- `smoothstep(outerConeCos, innerConeCos, cosTheta)` cone attenuation
- SpotLightSSBO (64 bytes each: position+radius, color+falloff, direction+coneAngles, outerCone+pad)

**MoltenVK fallback:** Checks `HasHardwarePCF` at init; loads `deferred_lighting_nohwpc.frag` on Apple Silicon (manual PCF instead of `sampler2DShadow`).

### 5.6 ForwardBlend Pass (`VulkanForwardBlendPass`)

- **Writes:** `"Lighting"` — LOAD overlay (no clear), preserves GBuffer lighting output
- **RenderGraph declaration:** `WriteTexture` (not `ReadWriteTexture`). Declaring `ReadWriteTexture` would add `Lighting` to `TextureReads`, creating a backward DAG edge from `WBOIT_Resolve` (which also writes `Lighting`) to `ForwardBlend` — causing a circular dependency.
- **Depth barrier:** Depth is already `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` from LightingPass. Post-render barrier transitions depth `DEPTH_STENCIL_ATTACHMENT → DEPTH_STENCIL_READ_ONLY`.

**Three sub-renderers in a single render pass:**

1. **Skybox:** Rotation-only view matrix (infinite), `DepthTest::LEqual` (writes at far plane), no cull. Push constants: VP + Intensity. Reads `u_Skybox` cube map.
2. **Grid:** 2000×2000 plane, `DepthTest::Less`, alpha blend. Dual-grid system (10-unit major + 1-unit minor), X/Z axis coloring, `fwidth()` anti-aliased lines, distance fade to 100 units. Writes correct Vulkan depth via `gl_FragDepth`.
3. **Sprites:** Painter's Algorithm (far-to-near sort). Per-sprite draw calls with texture + transform + color push constants. Triangle strip (4 vertices from `gl_VertexIndex`).

### 5.7 Outline Pass (`VulkanOutlinePass`)

- **Writes:** `"Selection"` — RGBA8 (full resolution)
- Renders selected entities as solid white `(1,1,1,1)` on black background
- `DepthTest OFF` — shows full silhouette even through walls
- `NoTextureDescriptors = true` — no texture binding needed
- Container node entities: if selected entity has no MeshRenderer but children have meshes, all child meshes are rendered
- Feeds `PostProcessPass` for Sobel edge detection → orange outlines

---

## 6. Vulkan Backend Infrastructure

### VulkanContext

**Lifecycle management:**
- Instance creation with Validation Layers (`VK_LAYER_KHRONOS_validation` always enabled)
- GPU-Assisted Validation (`VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME` with `GPU_ASSISTED_EXT`) when available
- Debug messenger: `ERROR`/`WARNING` severity routing to engine logging (`[Vulkan]` prefix)
- Physical device selection, logical device creation
- VMA allocator with `EXT_MEMORY_BUDGET_BIT` for GPU memory budget querying
- Command pools (graphics + compute), descriptor pools

**Synchronization:**
- **Per-swapchain-image semaphores** (`m_ImageAvailableSemaphores`, `m_RenderFinishedSemaphores`)
- **Per-frame fences:** `m_InFlightFences[3]`, created with `VK_FENCE_CREATE_SIGNALED_BIT`

**Frame lifecycle:**
1. `BeginFrame()`: Wait fence for current frame slot, process deferred releases, acquire next image, reset command buffer
2. Application renders (passes execute via RenderGraph)
3. `SwapBuffers()`: End command buffer, submit, present, advance `m_CurrentFrame = (m_CurrentFrame + 1) % 3`

**Swapchain:** `minImageCount = capabilities.minImageCount + 1` (typically triple-buffered). Preferred format: `B8G8R8A8_UNORM + SRGB_NONLINEAR`. VSync: `FIFO_KHR` (on) or `IMMEDIATE_KHR > MAILBOX_KHR > FIFO_KHR` (off). Automatic recreation on resize/minimize (spin-waits while framebuffer size is 0).

**Dynamic Rendering:** `vkCmdBeginRendering`/`vkCmdEndRendering` replace `VkRenderPass`/`VkFramebuffer`. `VulkanPipeline` uses `VkPipelineRenderingCreateInfo` in pNext chain.

**Deferred release:** 3-frame deferred resource cleanup (aligned with `m_FramesInFlight`). Old IBL cube maps, FBOs, textures are queued and released after GPU is guaranteed done.

**Batch upload system:** `BeginTextureUploadBatch`/`UploadTextureToBatch`/`EndTextureUploadBatch` — one large staging buffer + one command buffer for N textures. Prevents OOM during bulk glTF imports on unified-memory Apple Silicon.

### VulkanCapabilities

```cpp
struct VulkanCapabilities {
    bool HasDrawIndirectCount = false;   // VkPhysicalDeviceVulkan12Features::drawIndirectCount
    bool HasBindlessTextures = false;     // shaderSampledImageArrayNonUniformIndexing
    bool HasHardwarePCF = false;          // Hardcoded: false on __APPLE__, true otherwise
};
```

### MoltenVK / Apple Silicon Compatibility

- `VK_KHR_portability_subset` enabled on macOS logical device
- `VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME` + `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` at instance creation
- Hardware PCF unavailable → manual PCF shader path (`deferred_lighting_nohwpc.frag`)
- `ByteAddressBuffer` unavailable → `StructuredBuffer<uint>` with manual `asfloat()` unpacking
- Shadow culling: fixed-slot path (no `atomicAdd`/`DrawIndirectCount`)
- Dummy descriptor set layouts for Sets 0/1 in compute pipelines (MoltenVK requires contiguous non-null handles)
- Compute-to-indirect barriers explicitly labeled as critical for Apple Silicon TBDR correctness
- Zero-fill of indirect draw buffers on empty frames to prevent stale triple-buffer data

### VulkanRenderCommandBuffer

**Descriptor set ring buffer pattern:**
- `m_PendingImageInfos` accumulates texture bindings in a map of `slot → VkDescriptorImageInfo`
- `BindTexture2D`/`BindTextureCube` only populate this map — no Vulkan API calls
- `FlushDescriptorSets()` writes all accumulated bindings to a fresh descriptor set from the ring buffer
- **PENDING IMAGE INFOS ARE NOT CLEARED** after flush — accumulated bindings persist so subsequent draws can bind a subset
- Only `BindPipeline` clears the map (descriptor layouts are pipeline-specific)

**BeginRenderPass layout contract:** Images must already be in `COLOR_ATTACHMENT_OPTIMAL` / `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` — `BeginRenderPass` does NOT perform layout transitions. RenderGraph's `EnsureWritable` handles this.

### VulkanPipeline — Extended Spec Flags

| Flag | Effect |
|------|--------|
| `NoTextureDescriptors` | Skip Set 1 binding (outline mask pass) |
| `NoGlobalUBOs` | Skip Set 0 binding (UI pipelines) |
| `UseBindlessTextures` | Use global bindless array instead of per-draw writes |
| `NoFragmentShader` | VS-only pipeline, enables hardware Early-Z (Shadow opaque, DepthPrePass) |
| `ColorWrite = false` | All color attachments writeMask=0 (DepthPrePass) |
| `DepthFuncLEqual` | Quick LEqual depth mode (skybox) |
| `PolygonModeLine` | Quick wireframe toggle |
| `DepthBiasEnable` / `DepthBiasConstantFactor` / `DepthBiasSlopeFactor` / `DepthBiasClamp` | Shadow depth bias |

**`s_ExtraSetLayouts` static side-channel:** Before `Pipeline::Create(spec)`, passes push their extra descriptor set layouts (GDR Set 2, pass-specific Set 3) into this static vector, then clear it immediately after. RAII-style guard on both success and exception paths.

### VulkanFramebuffer — Dynamic Rendering Layout Model

- FBO images created with usage flags `COLOR_ATTACHMENT_BIT | SAMPLED_BIT`
- At creation: one-time `UNDEFINED → SHADER_READ_ONLY_OPTIMAL` transition for ImGui compatibility
- During rendering: each pass transitions to `COLOR_ATTACHMENT_OPTIMAL` via `vkCmdBeginRendering`
- After rendering: explicit barriers → `SHADER_READ_ONLY_OPTIMAL` for subsequent shader reads
- TBDR tile-resolve on Apple Silicon handled by RenderGraph `InsertTileResolveBarrier` (same-layout execution+memory barriers)

### VulkanShader — Two-Tier SPIR-V Lookup

Unprefixed shader paths first check `project://Shaders/Cache/{path}.spv` (project-local override), then fall back to `engine://Editor/shaders/cache/vulkan/{path}.spv`. Per-project shader customization without engine modification.

### VulkanBindlessManager

A single large descriptor set (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, Set 1 Binding 0) with:

- **`PARTIALLY_BOUND_BIT`** — shaders can index beyond filled slots
- **`UPDATE_AFTER_BIND_BIT`** — set can be updated after bound to command buffer
- **`UPDATE_AFTER_BIND_POOL_BIT`** on layout and pool
- **Variable descriptor count** via `VkDescriptorSetVariableDescriptorCountAllocateInfo`
- **Stage flags:** `VK_SHADER_STAGE_FRAGMENT_BIT`

| Index | Purpose |
|-------|---------|
| 0 | Invalid/fallback |
| 1 | White 1×1 texture (identity) |
| 2 | Black 1×1 texture (zero) |
| 3 | Default normal map (flat Z-up) |
| 4+ | Allocatable slots (LIFO free-list) |

**Lifecycle:**
- `AllocateIndex()` → LIFO free-list first, then `m_NextIndex++`
- `UpdateBinding(device, index, imageView, sampler)` → single `vkUpdateDescriptorSets`
- `FreeIndex(index)` → push to free-list (indices 0-3 never recycled)
- Textures register on creation; indices stored per-material in `GPUMaterial`/push constants

### Descriptor Set Binding Contract

| Set | Binding | Content | Provider |
|-----|---------|---------|----------|
| 0 | 0 | Camera UBO (176 bytes): viewProj, view, cameraPos, screen params, time | Engine default |
| 0 | 1 | DirLight UBO / PointLight SSBO | Lighting pass |
| 1 | 0+ | Per-material textures OR bindless texture array `u_GlobalTextures[]` | RenderCommandBuffer / BindlessManager |
| 2 | 0-3 | GDR SSBOs: Instances, Ranges, Materials, GeometryPool | GDRContext |
| 3 | 0 | DrawIndirectBuffer (compute cull output) | Per-pass specific |

---

## 7. Shader Architecture

### Shader File Inventory (68+ source files, `assets/Editor/shaders/src/vulkan/`)

| Directory | Files | Purpose |
|-----------|-------|---------|
| `2D/` | 2 | Sprite rendering (vert+frag) — vertex-index-driven quad, texture sampling |
| `Custom/` | 2 | Example: grayscale post-process + test compute (no-op 8×8×1) |
| `Debug/` | 4 | Debug/wireframe rendering, PBR forward renderer (Cook-Torrance + IBL + PCF shadows) |
| `Deferred/` | 20 | GBuffer (GDR vert, bindless frag), depth_only frag, deferred lighting (HW+noHW PCF), cull.comp, Hi-Z build/downsample/cull (×2 phases), light_volume vert/frag/spot_frag |
| `Fallback/` | 2 | Diagnostic magenta material (HDR bright for bloom detection) |
| `Generic/` | 2 | **AyayaGDR.hlsl** standard library, generic_fullscreen.vert (vertex-index-driven triangle) |
| `IBL/` | 7 | Cubemap capture (equirect→cube), irradiance convolution (~728 samples), prefilter (GGX importance, 1024 samples), BRDF LUT (1024-sample Monte Carlo) |
| `PostProcess/` | 6 | Tone mapping (ACES+Reinhard), bloom downsample (13-tap Karis), bloom upsample (9-tap weighted), FXAA, clear stub |
| `Preview/` | 4 | Asset preview (simple 3-point lighting + PBR with studio lights) |
| `Shadow/` | 8 | Shadow GDR opaque/masked, culling (atomic+fixed variants), legacy shadow_map |
| `Skybox/` | 2 | Cubemap skybox (depth=1.0, rotation-only view) |
| `SSAO/` | 2 | SSAO generate (64-sample hemisphere) + bilateral blur (cross-bilateral depth+normal) |
| `SSR/` | 6 | SSR ray-march (ssr_march) + roughness bilateral blur (ssr_blur) + UE-style composite (apply_reflection) |
| `UI/` | 8 | UI quads (bindless), editor grid (dual-grid + fwidth AA), outline mask (solid color), selection mask |
| `WBOIT/` | 5 | Gather (non-instanced + instanced + bindless variants), resolve (weighted blend) |

### AyayaGDR.hlsl — Standard GDR Library

All GDR shaders `#include` this library. TAs never write their own `[[vk::binding]]` or cbuffer declarations.

**Declarations:**

| Set | Binding | Type | Name | Content |
|-----|---------|------|------|---------|
| 0 | 0 | `cbuffer` | `CameraUBO` | `viewProj` (float4x4), `view` (float4x4), `cameraPos` (float3+pad), `_ScreenParams` (float4), `_Time` (float4) — 176 bytes |
| — | — | `push_constant` | `pc` (FrustumPC) | `planes[6]` (float4[6]=96B), `instanceCount`, `lightModeMask`, `overrideInstanceID`, `_pad`, `_TexelSize` (float4), `_ExposureInverse` (float) |
| 2 | 0 | `StructuredBuffer<GPUInstance>` | `u_Instances` | Per-instance transform, bounding sphere, range/material indices, flags |
| 2 | 1 | `StructuredBuffer<GeometryRange>` | `u_Ranges` | Per-submesh vertexOffset (uint-elements), indexOffset (bytes), counts |
| 2 | 2 | `StructuredBuffer<GPUMaterial>` | `u_Materials` | Full PBR + bindless indices + LightMode mask + packing enum + customData |
| 2 | 3 | `StructuredBuffer<uint>` | `g_Data` | Raw geometry pool (uint format for MoltenVK compat) |

**`GetAyayaVertex(vertexID, instanceID)` helper:**
1. Resolves instance ID: `overrideInstanceID != 0xFFFFFFFF` ? override : `SV_InstanceID`
2. Indexes `u_Instances[finalID]` → transform + geometry range index
3. Computes base offset: `range.vertexOffset + vertexID * kVertexStride(11)`
4. Reads 2 `uint4` from `g_Data` (8 uint reads): `{pos.xyz, normal.x}` and `{normal.yz, uv.xy}`
5. Unpacks via `asfloat()` — no `ByteAddressBuffer` needed (MoltenVK compat)
6. Tangent (uints 8-10) is NOT read (not needed for GBuffer/lighting)
7. Returns `AyayaVertex{position, normal, uv, worldMatrix, materialIdx, flags}`

**Convenience macros:** `AYAYA_TIME` (= `_Time.y`), `AYAYA_DELTA` (= `_Time.x`), `AYAYA_SCREEN_W`, `AYAYA_SCREEN_H`

### Key Shader Techniques

- **Octahedral encoding/decoding** (`OctEncode`/`OctDecode`): 2-channel RG16F for world-space normals — compact, high quality, used across GBuffer output and lighting input
- **Cook-Torrance BRDF:** D_GGX (Trowbridge-Reitz), G_Smith (height-correlated), F_Schlick — UE4-style PBR
- **Shadow PCF:** 3×3 kernel with bias `max(0.0003*(1-NdotL), 0.0001)`. Vulkan Y-flip: `p.y = p.y * (-0.5) + 0.5`
- **IBL:** Split-Sum Approximation — irradiance cube (diffuse) + prefiltered cube at `roughness*4.0` LOD + BRDF LUT 2D lookup
- **TexturePacking:** Three modes in all PBR shaders — UE4_ORM (R=AO, G=Roughness, B=Metallic), glTF_MetalRough (B=Metallic, G=Roughness, AO from separate), Separate (individual maps)
- **SSAO hemisphere sampling:** TBN from 4×4 noise texture, 64 deterministic samples via sin/cos of prime multiples
- **Bloom threshold:** Karis average 13-tap downsample with knee curve (soft threshold)
- **WBOIT pre-exposure:** 0.01 scale to prevent FP16 overflow in accumulation buffer

### MoltenVK Shader Variants

- `deferred_lighting_nohwpc.frag.spv` — manual PCF (`sampler2D` + `refZ > pcfDepth`) instead of `sampler2DShadow`
- `cull_shadow_fixed.comp.spv` — fixed-slot writes instead of `atomicAdd` on count buffer
- GBuffer/DepthPrePass culling (`cull.comp`) uses fixed-slot pattern — no atomic variant needed

### HLSL Compilation

```bash
dxc -spirv -T vs_6_0 -E VS_Main -fvk-use-dx-layout <file>.hlsl -Fo <output>.vert.spv
dxc -spirv -T ps_6_0 -E PS_Main -fvk-use-dx-layout <file>.hlsl -Fo <output>.frag.spv
dxc -spirv -T cs_6_0 -E CS_Main -fvk-use-dx-layout <file>.hlsl -Fo <output>.comp.spv
```

Includes `-I assets/Editor/shaders/src/vulkan/` for HLSL `#include` resolution (AyayaGDR.hlsl). GLSL shaders compile via `glslc` with `-fshader-stage=` and `-D` defines for variants.

### OpenGL Shader Comparison

The OpenGL shader set is significantly smaller — supports Deferred (GBuffer+Lighting), Shadow (basic), Skybox, IBL, PostProcess, 2D sprites, UI, Editor grid/outline, Preview. **Lacks:** SSAO, WBOIT, Hi-Z, GDR compute culling, bindless textures, SSR, texture packing variants, and the entire generic pass system.

---

## 8. SRP — Scriptable Render Pipeline

### Architecture

The SRP system allows the entire render pipeline DAG to be defined in Lua `.srp` scripts, replacing the hardcoded C++ pass graph.

### PipelineBuilder — Lua → RenderGraph Bridge

**Design goal:** Zero Lua calls in the hot path.

**Workflow:**
1. Lua script calls `DeclareTexture` / `AddPass` / `SetOutput` during setup (once per compile)
2. `PipelineBuilder` bakes all Lua params into pure C++ `PassBakedParams` structs
3. `Compile()` runs Kahn topological sort + physical FBO creation
4. `m_Graph.Execute()` runs per-frame with zero `sol::table`, zero `lua_State`, zero string hashing — pure C++

**`AddPass(name, passType, reads, writes, readWrites, params, w, h)` flow:**
1. Duplicate detection via `m_PassNames` set
2. Pass instance resolution: Generic passes get fresh instances; built-in passes use shared instances from `m_PassInstances`
3. Execute function: WBOIT special-cased (`ExecuteGather`/`ExecuteResolve`); others → `passInstance->Execute()`
4. `BakeParams(params)` — iterate Lua `sol::table` ONCE, extract to `PassBakedParams`
5. Bake reads/writes/readWrites string vectors
6. Add to RenderGraph with two lambdas:
   - `setup_lambda`: Declare reads/writes into `RGBuilder`. Factory `DeclareResources` intentionally NOT called — would duplicate declarations (triggering double `InsertTileResolveBarrier` → VUID-01197 layout errors)
   - `execute_lambda`: Inject baked params into `context.Settings` namespaced by node name. Convert `LightMode` string to bitmask. Inject `DepthTarget` FBO pointer. Call `execFn(ctx, cmd)`.

**`PassBakedParams` struct:**
```cpp
struct PassBakedParams {
    bool Enabled = true;
    std::string LightMode;  // e.g., "GBuffer,ShadowCaster"
    std::string Queue;      // "Opaque" or "Transparent"
    unordered_map<string, float> FloatParams;
    unordered_map<string, int>   IntParams;
    unordered_map<string, string> StrParams;
};
```

**`DeclareTexture(name, formats, w, h)`:** Parse format strings via `kFormatMap` ("RGBA8", "RGBA16F", "Depth", "R8", etc.) → `FramebufferSpecification`. Register with RenderGraph. w=0/h=0 = viewport-sized.

**`SetGlobalFloat`/`SetGlobalInt`:** Stores into `m_BakedParams["__Globals__"]`, injected into `RenderContext` each frame under `"Global.*"` prefix.

**`SetOutput(name)`:** Sets final output texture for editor viewport display.

### PassRegistry

Global factory registry mapping pass type names to `IPassFactory` instances. Lazy-instantiation: first `Get(name)` creates the factory via stored lambda.

**18 registered in `Init()`:** ShadowPass, GBufferPass, DepthPrePass, SSAOPass, SSRPass, SSRBlurPass, ApplyReflection, LightingPass, ForwardBlend, WBOIT_Gather, WBOIT_Resolve, OutlinePass, BloomPass, PostProcessPass, FXAAPass, UIPass + GenericDrawPass, GenericComputePass, GenericFullScreenPass.

### Generic Pass System — Zero-CPP Pipeline Extension

**GenericDrawPass:**
- Full geometry rendering with GDR compute culling + indirect draw
- Lua-controlled: shader path, depth/blend/cull/colorWrite state, LightMode mask, render queue
- Pipeline caching by `PipelineKey(shader, depthTest, depthWrite, cullMode, blendMode, colorFormat, hasDepth, depthFunc, colorWrite)`
- GDR Set 2 injection via `VulkanPipeline::s_ExtraSetLayouts` static side-channel
- Opaque path: compute cull → `vkCmdDrawIndexedIndirect`
- Transparent path: CPU filter RenderQueue by LightModeMask → per-packet `vkCmdDrawIndexed` with overrideInstanceID

**GenericComputePass:**
- Compute shader dispatch. Lua: shader path, workgroup size, dispatch counts (manual or texture-derived)
- Pipeline/layout/shader caching by shader path string
- Current limitation: zero descriptor set / push constant support

**GenericFullScreenPass:**
- Full-screen post-process. Lua: fragment shader, optional custom vertex shader, blend mode, up to 4 input textures
- Pipeline caching by `{vertShader|fragShader|blendMode|format|hasDepth}` composite key
- Binds input textures from RenderGraph FBOs

### Default SRP Script (`assets/Editor/srp/default.srp`)

Declares 11 textures and 14 passes in topological order, reproducing the hardcoded C++ pipeline identically. Custom passes can be injected at any point. SSR passes are declared with `Enabled = false` by default.

### Safe Rebuild Pattern

```cpp
auto snapshot = m_RenderGraph.ExtractState();  // Move-steal old passes/textures
PipelineBuilder builder(m_RenderGraph);
builder.RegisterPassInstance("ShadowPass", m_ShadowPass);  // ... all passes ...
// Expose builder as Lua global "Pipeline"
bool ok = luaState.safe_script_file(scriptPath, sol::script_pass_on_error);
if (!ok) {
    m_RenderGraph.RestoreState(std::move(snapshot));  // FBOs, passes, layout tracking fully reinstated
}
```

Old FBOs survive in the snapshot (shared_ptr). Only dropped if rebuild succeeds. No black frame on Lua errors.

---

## 9. Image-Based Lighting (IBL)

### IBLBuilder (Abstract Interface)

Static factory methods dispatching to platform backend via `RendererAPI::GetAPI()`:

| Method | Output | Spec |
|--------|--------|------|
| `ConvertEquirectangularToCubemap` | Environment cube map | 1024×1024, 11 mip levels, RGBA16F |
| `CreateIrradianceMap` | Diffuse irradiance cube | 32×32, 1 mip, RGBA16F |
| `CreatePrefilterMap` | Specular prefiltered cube | 128×128, 5 mip levels, RGBA16F |
| `CreateBRDFLUT` | BRDF integration LUT | 512×512, RG16F |

### VulkanIBLBuilder

**Cubemap capture:** Renders 6 faces by rendering a cube with equirectangular map as input. Uses temporary 2D FBO (1024×1024), copies each face to cube layer via `vkCmdCopyImage`. Generates mipmap chain via `vkCmdBlitImage` (LINEAR filter, pair-wise).

**Irradiance convolution:** Hemisphere cosine-weighted Riemann sum (~728 samples at sampleDelta=0.05). Uses static `s_SourceCubemapSampler` from original environment cube map.

**Prefiltered environment:** GGX importance sampling with Hammersley sequence (1024 samples). Nested loop: 5 mip levels × 6 faces. Viewport trick: renders at full FBO resolution but sets viewport to mip size in lower-left corner, then `vkCmdCopyImage` copies only that region.

**BRDF LUT:** Empty vertex layout (vertex-index-driven fullscreen triangle → 3 vertices via `vkCmdDraw`). 1024-sample Monte Carlo. Stores `vec2(A, B)` — scale and bias for split-sum approximation.

**Resource tracking:** All IBL-created resources stored in static `s_TrackedIBLResources`, freed at shutdown via `ClearResources()`.

### Pipeline Integration

- Default fallback: 1×1 black cube map (no environment contribution)
- Environment change: async IBL generation + 3-frame deferred release of old cube maps
- Per-frame injection into `RenderContext`: `"EnvironmentCubemap"`, `"IrradianceMap"`, `"PrefilterMap"`, `"BRDFLUT"`, `"HasEnvironmentIBL"`, `"EnvironmentIntensity"`, `"EnvironmentAmbientColor"`
- Consumed by: `LightingPass` (Set 1, bindings 8-10), `ForwardBlendPass` skybox, `WBOITPass` gather (Set 3), `ForwardTestPass`

---

## 10. WBOIT — Order-Independent Transparency

### Two-Pass Approach

**Pass 1 — Gather (`VulkanWBOITPass::ExecuteGather`):**
- **Writes:** `"WBOIT_Gather"` — RGBA16F (weighted accumulation) + RG16F (revealage), CLEAR to black/(1,0,0,0)
- **Depth:** Shared from `SceneDepth`, `DEPTH_STENCIL_READ_ONLY_OPTIMAL`, `LOAD_OP_LOAD`, `STORE_OP_NONE` — transparent objects depth-test against opaque scene
- **Manual dynamic rendering:** Uses `vkCmdBeginRendering` directly (not `cmd.BeginRenderPass`) for dual-color-attachment format control
- **Instanced batching:** Builds map of `(Mesh*, MaterialHash) → Batch{firstInstance, instanceCount}`. Packs all instance transforms contiguously into `m_InstanceBuffer` SSBO. Single `vkCmdDrawIndexedIndirect` per batch.
- **Bindless fragment shader** (`wboit_gather_bindless.frag`): Full PBR (directional light + IBL) via bindless texture array + Set 3 IBL (IrradianceMap, PrefilteredMap, BRDFLUT)
- **Pre-exposure scale** (0.01) prevents FP16 overflow in accumulation
- **Weight function:** `alpha * max(0.1, 10*(1-depth))` — depth-based weight for correct occlusion
- **Blend modes:** Accum = Additive (`ONE, ONE`), Revealage = WBOITRevealage (`ZERO, ONE_MINUS_SRC_COLOR`)
- **SRP routing:** Objects with custom LightModeMask (not including Forward bit 4) are skipped

**Pass 2 — Resolve (`ExecuteResolve`):**
- **ReadWrite:** `"Lighting"` — LOAD overlay
- **Fullscreen composite:** `C = accum / clamp(revealage, ε, ∞)`, then divide by `WBOIT_PRE_EXPOSURE` to recover HDR
- Standard alpha blend onto `Lighting` HDR buffer

### Descriptor Set Layouts

- **Set 2 (instances):** Single SSBO at binding=0, Vertex stage (`glm::mat4[]`)
- **Set 3 (IBL):** 3 combined image samplers, Fragment stage — IrradianceMap(b0), PrefilteredMap(b1), BRDFLUT(b2)

**Push constants (`WBOITGatherPushConstants`, 256B max):**
```cpp
mat4 Transform;           // 64B — unused in instanced path
vec4 Albedo; float Metallic, Roughness, AO; uint UseORMMap;
uint AlbedoMapIndex, NormalMapIndex, ORMMapIndex;
uint MetallicMapIndex, RoughnessMapIndex, AOMapIndex;
float Alpha; uint Packing;
```

---

## 11. Post-Processing Pipeline

### Bloom (`VulkanBloomPass`)

- **Writes:** `"Bloom"` — RGBA16F, half-res
- **5-level mip chain:** `"Bloom"` (w/2×h/2) + 4 internal mips (w/4 → w/32)
- **Downsample (`bloom_downsample.frag`):** 13-tap Karis average. At mip 0: threshold cutoff with knee curve (`Curve = {threshold-knee, knee*2, 0.25/knee}`). NaN/Inf guard via `SafeSample()`.
- **Upsample (`bloom_upsample.frag`):** 9-tap 3×3 weighted filter (center ×4, edges ×2, corners ×1). Additive blend.
- Internal mips (1-4) manually transitioned — NOT RenderGraph-managed. Mip 0 (`"Bloom"`) skips the final manual transition — RenderGraph handles it.

### Tone Mapping (`VulkanPostProcessPass`)

- **Reads:** `"Lighting"` (HDR RGBA16F), `"Selection"` (RGBA8 mask), `"Bloom"` (RGBA16F)
- **Writes:** `"FinalOutput"` — RGBA8 LDR
- **Operations:**
  1. Apply Exposure
  2. Tone mapping: ACES filmic (fitted: `(x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14)`) or Reinhard-like (`1.0 - exp(-hdrColor)`)
  3. Bloom blend: add `u_BloomTexture * BloomIntensity`
  4. Selection outline: 8-neighbor Sobel on `u_SelectionTexture` red channel, orange `(1.0, 0.65, 0.0)` at edges where center < 0.1 and edge sum > 0.1
  5. sRGB gamma: `pow(mapped, 1.0/2.2)`

### FXAA (`VulkanFXAAPass`)

- **Reads:** `"FinalOutput"` (LDR RGBA8)
- **Writes:** `"FXAA"` — RGBA8
- Luma-based edge detection (5-neighbor), horizontal/vertical classification, single sub-pixel sample offset
- Push-constant bypass: when `Enable == 0.0`, shader passes through unchanged

---

## 12. Asset System Integration

### Material Class

**Core:** `Name`, `ShaderName`, `BlendMode` (Opaque=0, Masked=1, Translucent=3), `LightModeMask` (uint32 bitmask), `AlphaCutoff`, `TexturePacking` (UE4_ORM/glTF_MetalRough/Separate)

**MaterialProperty (tagged union):** `UniformName`, `DisplayName`, `Type` (Float/Int/Bool/Vec2/Vec3/Vec4/Mat3/Mat4/Texture2D/TextureCube). All value fields stored simultaneously; only one matching `Type` is meaningful. Textures use UUID handles + runtime `shared_ptr` override for dynamic FBO injection.

**Bind():** Iterates properties, calls `shader->Set*` for scalars/matrices, resolves textures via `AssetManager` for `cmd.BindTexture2D`.

**BakedPC (pre-baked push constant cache):** Inner struct with all PBR scalars + per-texture bindless indices (default to white=1). `Dirty` flag triggers lazy rebuild from Properties. Used by `GDRContext::BuildFromRenderQueue()`.

**LightModeMask:** Priority: explicit mask → `LightModeStr` via `LightModeTagRegistry::ParseMask()` → auto-detect from BlendMode (Opaque/Masked → GBuffer|ShadowCaster, Translucent → Forward|ShadowCaster)

### glTF Import Pipeline

**Two-phase async import bypassing Assimp:**

1. **Phase 1 (background thread, `glTFParser`):** cgltf-based parsing → engine entities with PBR materials, `KHR_lights_punctual` lights, node hierarchy. CPU-only, no GPU calls.

2. **Phase 2 (`glTFAssetImporter`):** Two sub-phases:
   - Copy textures, create `.mat`/`.meta` files, extract meshes (background)
   - Register assets in `AssetManager`, trigger GPU uploads, load prefab scene (main thread)

**Features:** UUID reuse on re-import (reads existing `.meta`), texture downscaling (max 2048px via stb_image_resize2), sRGB/linear auto-detection from filename heuristics, alpha mode mapping, path deduplication

### AssetWatcher — File-System Hot Reload

- **300ms debounce** per file, coalescing rapid-fire events
- **Topological sort by asset weight:** 0=Texture→1=Material→2=Prefab→3=Scene→4=SRPipeline (lower reloads first for correct dependency ordering)
- **Cascade resolution:** Reverse dependency graph via `AssetManager::GetDependents()` for transitive reloads
- **Retry:** Up to 10× for in-progress file writes (`CanReadFileExclusive`)
- **Pause/resume:** Suspends during intentional mutations (rename/move/delete) and bulk imports

### `.ayashader` Format

YAML-based shader+material template bundling HLSL path, render state, LightMode mask, and material property templates:

```yaml
Name: "ShaderName"
PassType: "GenericDrawPass"
Tags: { LightModeMask: 64, Queue: "Opaque" }
RenderState: { DepthTest: true, CullMode: "Back", BlendMode: "Opaque" }
Properties:
  - { Name: "_Color", DisplayName: "Albedo Tint", Type: Color, Default: [1,1,1,1] }
HLSL: "shader_file.hlsl"
```

Dropping in ContentBrowser generates corresponding `.mat` files. `AssetType::AyaShader` = 20.

### LightModeTagRegistry

Singleton mapping human-readable tags to uint32 bit positions:
- **Built-in (bits 0-3):** GBuffer(1), ShadowCaster(2), Forward(4), DepthPrePass(8)
- **Custom (bits 4+):** `RegisterTag("Hologram")` → auto-assigns next available bit
- **`ParseMask("GBuffer,ShadowCaster")`:** Splits by comma, auto-registers unknown tags, returns combined bitmask
- **`MaskToString(mask)`:** Reverse lookup with registered names, raw integer fallback for unmapped bits

### AssetManager

Fully static singleton. UUID-based type-erased storage (`shared_ptr<void>`). Lazy loading from disk on first `GetAsset<T>(UUID)`. Async texture loading: background `stbi_load` → `RawTextureData` (move-only, auto-frees) → main-thread GPU upload. Max 4 concurrent background loads. 3-frame deferred release for safe GPU resource cleanup.

---

## 13. Editor Integration

### Two-Viewport Architecture

`EditorLayer` manages two independent `SceneRenderer` instances:

| Renderer | Viewport | Camera | Features |
|----------|----------|--------|----------|
| `m_SceneRenderer` | Editor | `EditorCamera` (orbit) | Grid, gizmos, mouse picking, selection outline, camera/light icons |
| `m_GameRenderer` | Game | Scene's `Primary` camera | Player preview, runtime rendering |

Each viewport tracks own focus/hover state. Custom resolution rendering (1920×1080, 2560×1440, 3840×2160) with letterboxing.

### Play Mode Scene Duplication

`OnScenePlay()` performs YAML serialization round-trip:
1. Serialize `m_EditorScene` → `project://temp/temp_play_scene.ayaya`
2. Deserialize into fresh `m_ActiveScene`

Full isolation — edits during Play mode affect the runtime clone, never the editor scene. `OnSceneStop()` restores `m_ActiveScene = m_EditorScene`. Physics only runs in Play mode via `OnPhysics2DStart`/`OnPhysics2DStop` (Box2D world creation/destruction).

### Frame Debugger Panel

Three tabs:
1. **Pass Outputs:** Per-pass FBO attachment preview with freeze/live toggle. Left panel: pass tree with per-attachment sub-items. Right panel: full-resolution preview with aspect-correct display + debug stats (CPU/GPU time, draw calls, triangles, textures read/written).
2. **Pipeline Profiler:** Sorted timing table by execution order. Color-coded (green/yellow/red at 1ms/2ms thresholds). Aggregated totals header row.
3. **All Textures:** Complete framebuffer inventory with producer, resolution, format list, estimated VRAM, compact thumbnail preview.

### AssetPreviewer

Static headless renderer for 3D model/material/prefab thumbnails:
- 256×256 4×MSAA FBO with resolve, completely independent from main rendering pipeline
- Auto-frames camera to model AABB (distance = radius/sin(fovY/2))
- Dedicated descriptor pool + UBO for GPU-resident thumbnails (isolated from frame loop's descriptor ring buffer)
- Zero-copy ImGui display for realtime preview via `VulkanTexture2D` FBO wrapper
- GPU-resident async pipeline: renders directly into destination textures without CPU readback (currently disabled — GDR meshes lack traditional VBO/IBO)

---

## 14. Screen-Space Reflections (SSR)

> **Status:** Fully implemented (Aug 2026). UE-style hierarchical SSR with Hi-Z acceleration, Blue Noise jitter, roughness-driven bilateral blur, and premultiplied alpha compositing.

### 14.1 Architecture Overview

Three-pass SSR pipeline inserted between Lighting and ForwardBlend:

```
Lighting_NoSpecIBL → SSR (½-res ray march) → SSRBlur (½-res bilateral) → ApplyReflection (full-res composite) → ForwardBlend
```

**Key design:** IBL specular is *removed* from the Lighting pass (`deferred_lighting.frag`, line 160: `+spI` deleted). The ApplyReflection pass is the sole provider of specular IBL — computing it from the PrefilteredMap cubemap, and replacing it with SSR where the ray-march finds a screen-space hit.

### 14.2 File Inventory (20 files)

**Shader source (6):**
| File | Size | Purpose |
|------|------|---------|
| `SSR/ssr_march.vert` | 394B | Fullscreen triangle, half-res |
| `SSR/ssr_march.frag` | ~7.9KB | **Core** — Hi-Z accelerated ray-march, outputs `SSR_Result` (RGBA16F) |
| `SSR/ssr_blur.vert` | 299B | Fullscreen triangle, half-res |
| `SSR/ssr_blur.frag` | ~2.0KB | Roughness-driven bilateral blur, outputs `SSR_Blurred` |
| `SSR/apply_reflection.vert` | 299B | Fullscreen triangle, full-res |
| `SSR/apply_reflection.frag` | ~3.8KB | UE-style hierarchical replacement composite |

**C++ Pass classes (6):**
| File | Purpose |
|------|---------|
| `VulkanSSRPass.hpp/.cpp` | SSR ray-march pass: Hi-Z descriptor set, Blue Noise, push constants |
| `VulkanSSRBlurPass.hpp/.cpp` | 2-pass separable bilateral blur (Horiz + Vert), internal FBO |
| `VulkanApplyReflectionPass.hpp/.cpp` | Full-res composite: PrefilteredMap + BRDF LUT + SSR_Blurred → additive blend onto Lighting |

**Pipeline integration (8 modified):**
| File | Change |
|------|--------|
| `deferred_lighting.frag` (+ 2× SPIR-V) | Removed `+spI` from ambient term |
| `SceneRenderer.hpp/.cpp` | 3 new pass members, construction, OnAttach, RenderGraph, SRP, culling |
| `PassRegistry.hpp/.cpp` | 3 new factories (`SSRPass`, `SSRBlurPass`, `ApplyReflection`), Init signature extended |
| `RenderGraph.hpp/.cpp` | `WriteLoadOps` support, single-pass producer mapping DAG fix |
| `VulkanGBufferPass.hpp` | Public Hi-Z getters (`GetHiZImageView`, `GetHiZSampler`, `GetHiZMipCount`) |
| `default.srp` | `SSR_Result`, `SSR_Blurred` textures + 3 pass declarations |
| `Components.hpp` + `SceneSerializer.cpp` | 7 SSR ECS parameters |
| `PropertiesPanel.cpp` + `FrameDebuggerPanel.cpp` | SSR UI controls + debug entries |

### 14.3 SSR Pass — Hi-Z Accelerated Ray March (`ssr_march.frag`)

**Inputs (Set 1):** `u_DepthMap(0)`, `g_Albedo(1)`, `g_PBR(2)`, `g_Normal(4)`, `u_Lighting(6)`, `u_BlueNoise(7)`
**Inputs (Set 2):** `u_HiZ(0)` — Hi-Z depth pyramid from GBufferPass
**Output:** `SSR_Result` — half-res RGBA16F, premultiplied alpha (`hitColor * alpha, alpha`)

**Algorithm steps:**

1. **Surface reconstruction:** `ViewPosFromDepth` (NDC Y-flip compensated) → viewPos. OctDecode GBuffer normal → `mat3(View)*N` to view space. Roughness from `g_PBR.g`.

2. **Filtering:** `roughness > RoughnessCutoff` → discard. No metallic cull — Fresnel controls reflection strength for all surfaces.

3. **Reflection direction:** Blue Noise jitter on normal: `N_jittered = N + jitter * roughness * 0.3`. `R = reflect(-V, N_jittered)`.

4. **Hi-Z traversal:** Ray projected to screen UV start→end. DDA cell crossing algorithm traverses the depth pyramid:
   - Start at coarsest mip (e.g., mip 10)
   - Sample Hi-Z cell: `minDepth = textureLod(u_HiZ, cellCenter, mip).r` (MAX-reduced, conservative)
   - `rayZ > minDepth` → ray behind everything in cell → DDA-skip to cell boundary, upgrade mip
   - `rayZ <= minDepth` → potential intersection → descend one mip (`prevUV` reset)
   - At mip 0: linear NDC Z comparison (`mix(zStart, zEnd, t)`) with thickness test against `u_DepthMap`

5. **Hit detection (mip 0):** Crossing test (`prevRayZ <= prevSceneZ + Thickness && rayZ > sceneZ`) prevents false hits at depth discontinuities. Binary refinement in NDC Z space (perspective-correct midpoint via `mix(fPos, bPos, t)` where `t` from NDC Z midpoint). Samples `u_Lighting` at refined UV.

6. **Output:** `alpha = clamp(edgeFade * max(fresnel * 3.0, 0.1) * hitFound, 0.0, 1.0)`. Premultiplied: `FragColor = vec4(hitColor.rgb * alpha, alpha)`.

**Push constants (232 bytes):** `InvProj(64)`, `Proj(64)`, `View(64)`, `MaxSteps(4)`, `StepSize(4)`, `Thickness(4)`, `EdgeFade(4)`, `MaxBinarySteps(4)`, `RoughnessCutoff(4)`, `Enabled(4)`, `HiZMipCount(4)`, `_pad2(4)`.

### 14.4 SSRBlur Pass — Roughness-Driven Bilateral Blur (`ssr_blur.frag`)

**Algorithm:** 2-pass separable bilateral blur (C++ dispatches Horz + Vert with `BlurDir` push constant). Continuous radius with edge fading prevents integer-truncation banding.

**Key features:**
- `fRadius = roughness * 8.0` (continuous float, not truncated to int)
- `maxRadius = ceil(fRadius)` — loop range
- `edgeFade = clamp(fRadius - dist + 1.0, 0.0, 1.0)` — smooth transition when radius crosses integer boundaries
- Dynamic Gaussian sigma: `sigma = max(fRadius * 0.5, 0.5)`
- Depth weight: `exp(-abs(centerDepth - sampleDepth) * DepthThreshold)` — edge-preserving bilateral
- **Premultiplied alpha aware:** Weights do NOT multiply `sampleSSR.a` (color already premultiplied)

**Internal FBO:** Half-res RGBA16F (`m_BlurXFBO`), manually transitioned (not RenderGraph-managed). Final output writes to RenderGraph-managed `SSR_Blurred`.

### 14.5 ApplyReflection Pass — UE-Style Hierarchical Replacement (`apply_reflection.frag`)

**Inputs (Set 1):** `u_SSRResult(0)` — SSR_Blurred, `g_Albedo(1)`, `g_Normal(2)`, `g_PBR(3)`, `u_DepthMap(4)`, `u_SSAO(5)`, `u_PrefilteredMap(6)` (cube), `u_BRDFLUT(7)`

**Composite formula (premultiplied alpha):**
```
specularBRDF = F_SchlickR(NdotV, F0, roughness) * brdf.x + brdf.y
iblColor = PrefilteredMap(R, roughness*4.0) * EnvIntensity
ssrWeight = ssr.a * (1.0 - smoothstep(0.2, 0.6, roughness))
reflectionLight = ssr.rgb * roughnessFactor + iblColor * (1.0 - ssrWeight)
finalSpecular = reflectionLight * specularBRDF * ao * ssao
→ additive blend (One/One, LoadOp::Load) onto Lighting
```

**Key properties:**
- **Never culled:** Provides IBL specular fallback even when SSR is disabled (ssrFBO null → BlackTexture → ssrWeight=0 → pure IBL cubemap)
- **Premultiplied alpha:** `ssr.rgb` already has alpha baked in — hardware bilinear upsampling from half-res preserves energy at reflection edges, eliminating dark halos
- **Roughness-controlled transition:** `smoothstep(0.2, 0.6, roughness)` — mirror surfaces get full SSR, rough surfaces fall back to IBL cubemap

### 14.6 Lighting Pass Modification

`deferred_lighting.frag` line 160: `vec3 amb = (kDi * irr * Albedo) * AO * ssao;` — IBL specular (`spI`) removed from the ambient term. The Lighting buffer now contains only IBL diffuse + direct lighting. Specular IBL is computed independently by ApplyReflection.

### 14.7 Hi-Z Integration

The existing Hi-Z depth pyramid (built in `VulkanGBufferPass::BuildHiZ`, R32_SFLOAT, MAX-reduced mip chain) is exposed via public getters:
- `GetHiZImageView(frameIdx)` — full mip chain view
- `GetHiZSampler()` — NEAREST, CLAMP_TO_EDGE
- `GetHiZMipCount()` — `floor(log2(max(w,h))) + 1`
- `GetCurrentHiZIndex()` — ring buffer index of most recently built Hi-Z

SSRPass injects a Hi-Z descriptor set layout (Set 2, Binding 0) via `VulkanPipeline::s_ExtraSetLayouts` during `OnAttach`. At runtime, descriptor sets are written with the current frame's Hi-Z image+sampler and bound via `vkCmdBindDescriptorSets` at set=2.

### 14.8 Key Bug Fixes Applied

| Bug | Symptom | Fix |
|-----|---------|-----|
| NDC Y-flip missing in ray march UV | Vertically mirrored reflections | `1.0 - uv.y*2.0` in both UV→NDC and NDC→UV conversion |
| Roughness read from `g_Albedo.a` (always 1.0) | All reflections maximally blurred | Read from `g_PBR.g` (GBuffer attachment 2) |
| Alpha extrapolation > 1.0 at grazing angles | Red-black artifacts | `clamp(alpha, 0.0, 1.0)` |
| ApplyReflection culled with SSR | IBL disappears when SSR off | ApplyReflection never culled — always provides IBL fallback |
| "Hit within thickness" shortcut without binary refinement | Stair-step banding | All hits go through binary refinement |
| Missing crossing test | Vertical smearing | `prevRayZ <= prevSceneZ + Thickness` check |
| View-space binary search midpoint (no perspective correction) | Wobbly edges | NDC Z space midpoint via `mix(fPos, bPos, t)` |
| Metallic factor in alpha formula | Dielectrics get zero SSR | Removed — Fresnel alone controls reflection strength |
| `textureLod(u_Lighting, ..., roughness*4.0)` on non-mipmapped FBO | Undefined behavior | Changed to `textureLod(..., 0.0)` |
| Thickness `* 0.01` over-scaling | Missed hits at grazing angles | Direct NDC Z comparison: `rayZ < sceneZ + Thickness` |
| Non-premultiplied alpha + hardware bilinear | Dark halos at reflection edges | Full premultiplied alpha pipeline: `FragColor = vec4(color*alpha, alpha)` |
| `int(roughness*8.0)` truncation | Blur radius banding | Continuous `fRadius` + `edgeFade` + dynamic sigma |

---

## 15. Known Issues & Future Directions

### Known Issues

1. **Hi-Z occlusion culling fully built but disabled** via hardcoded `if (false && ...)`. Depth pyramid still constructed every frame (wasted GPU work).
2. **`VulkanClearPass` is an empty stub** — not wired into the RenderGraph DAG.
4. **Duplicate `#include "VulkanOutlinePass.hpp"`** in `SceneRenderer.cpp` (lines 36-37).
5. **`ChangeComponentCommand<T>` duplicated** in `Core/EditorCommands.hpp` and `Commands/ChangeComponentCommand.hpp`.
6. **No CI/CD, no tests, no linting** — no automated build pipeline, no unit/integration tests, no `.clang-format` or `.clang-tidy` configuration.
7. **UI Pass only renders `UIImageComponent`** — `UITextComponent` and `UIButtonComponent` rendering not yet implemented.
8. **`ssr_composite.frag` stub** outputs UV gradient instead of actual SSR composite.

### Limitation Awareness

1. **`CustomPostProcess` system** — API defined but `RemoveCustomPostProcess` has empty body. Reserved for future user scripting integration.
2. **GPU timing** — uses CPU timestamps via `std::chrono`; Vulkan GPU timestamp queries via `VkQueryPool` reserved for future implementation.
3. **`GenericComputePass`** — zero descriptor set / push constant support. Compute shaders cannot currently read input textures or access data beyond the compute shader itself.
4. **Metal backend** — directory exists at `src/Engine/Platform/Metal/` but is empty — not implemented.
5. **`AssetPreviewer` GPU-resident thumbnail pipeline** — disabled because GDR meshes lack traditional VBO/IBO.

### Design Strengths

1. **Zero-Lua hot path** — all SRP parameters baked to C++ structs at compile time. Per-frame execution has zero `sol::table`, zero `lua_State`, zero string key hashing.
2. **Atomic SRP rebuild** — `ExtractState`/`RestoreState` pattern prevents black frames on Lua script errors. Old FBOs survive in snapshot via shared_ptr.
3. **Triple-buffered everything** — FBOs (3× `VulkanFramebuffer`), UBOs (3× `VkBuffer`), SSBOs (3× persistent-mapped), descriptor sets (ring buffer). All in sync with `kRenderGraphFramesInFlight = 3`.
4. **Memory-efficient** — bindless textures eliminate per-material descriptor set churn. Unified geometry pool (512 MB monolithic buffer) eliminates per-mesh buffer overhead.
5. **Correct-by-construction barriers** — RenderGraph layout tracking is a pure state machine; no Vulkan image layout queries needed. Separate color/depth layout tracking for mixed FBOs.
6. **Extensible** — Generic passes (Draw/Compute/FullScreen) for TA pipeline extension without C++ recompilation. 64-byte `customData` per material for custom shader parameters.
7. **Apple Silicon aware** — MoltenVK compatibility throughout: manual PCF, fixed-slot shadow culling, `StructuredBuffer<uint>` instead of `ByteAddressBuffer`, TBDR barrier patterns, batch upload OOM prevention.
8. **Renderer isolation** — Editor has two independent `SceneRenderer` instances with separate `RenderGraph`s, `GDRContext`s, and pass instances. Edit mode and game mode are fully isolated.

---

*Report generated from comprehensive multi-agent analysis of all 14 Vulkan backend subsystems. Analysis depth: full source code reading of every file in the rendering pipeline, shader system, asset pipeline, and editor integration.*
