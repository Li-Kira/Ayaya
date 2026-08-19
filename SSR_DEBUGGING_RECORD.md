# SSR 黑圈排查记录

> **日期：** 2026-08-19
> **分支：** `feature/gpu-driven-bindless`
> **状态：** 已修复（根因 = RenderGraph 依赖边在 pass 运行时 un-cull 时算错）
> **适用范围：** 通用 —— 任何「运行时把某个 pass 从 culled 切到 active」都可能踩同一个坑，不限于 SSR。

---

## 1. 现象

- 运行时把 `EnableSSR` 从 `false` 切到 `true`（SSR / SSRBlur / SSRTemporal 三个 pass 从 culled → active）后，物体轮廓出现一圈**黑边/黑块**。
- 同一个场景**一开始就 `EnableSSR=true`** 则没有黑圈。
- 触发一下「移动摄像机 / 移动物体」或「整图重建」后，黑圈会消失（暂时或永久，视触发方式而定）。

---

## 2. 排查时间线（假设 → 验证 → 结论）

| # | 假设 | 验证方式 | 结论 |
|---|------|---------|------|
| 1 | ApplyReflection 被 cull / SRP 缺 SceneDepth | 检查 culling + default.srp | ❌ 无关（testSSR 硬编码路径也复现） |
| 2 | SSR temporal 历史没重置 | 加 `ResetHistory()` + `HasHistory` 直通 + alpha clamp | ❌ 没根治（是独立小问题，但不是黑圈根因） |
| 3 | 环境/IBL 是黑的 | testSSR 有环境且正常、降 Intensity 无效 | ❌ 排除 |
| 4 | 某个 FBO 内容/layout 脏 | 逐纹理 `InvalidateTexture`（SSR+Lighting / SceneDepth / GBuffer） | ❌ 都无效 |
| 5 | **整图重建能修** → 脏的是「RGPass 声明顺序」，不是 FBO | `m_ViewportDirty=true`（Clear + 重新 AddPass） | ✅ 黑圈消失（但闪一帧） |
| 6 | 根因：`Compile()` 把 `m_Passes` 覆盖成拓扑序，破坏声明顺序 | 通读 `RenderGraph::Compile()` | ✅ 定位到根因 |

关键转折是 **#5**：既然「只重建 FBO」无效、「整图 Clear+AddPass」有效，就说明脏状态不在纹理/FBO 层，而在 **RGPass 对象的存活状态**里。顺着这条去查 `m_Passes` 的重排逻辑，直接命中根因。

---

## 3. 根因

`RenderGraph::Compile()` 的「Preserve culled passes」步骤，把 `m_Passes` 从**声明顺序**覆盖成了「拓扑序 + culled 追加到末尾」：

```cpp
// 旧代码（bug）
{
    std::vector<std::shared_ptr<RGPass>> allPasses = std::move(sorted);
    for (auto& p : m_Passes)
        if (p->IsCulled) allPasses.push_back(p);
    m_Passes = std::move(allPasses);   // ← 覆盖了声明顺序
}
```

而 `Compile()` 的 **producer 映射要求「consumer 声明在生产之后」**，它按 `m_Passes` 的当前顺序增量填 `producers`：

1. SSR 关闭时，第一次 `Compile()` 把 SSR 三个 pass 挪到 `m_Passes` 末尾。
2. 之后 `ApplyPerFrameCulling` 只翻转 `IsCulled`，不再 `AddPass`。SSR 打开时，`Compile()` 按「错误的 `m_Passes` 顺序」构建依赖边，SSR 链被当成「声明在 ApplyReflection 之后」。
3. 后果：
   - `SSRTemporal → ApplyReflection` 这条**读边丢失**；
   - `ApplyReflection → SSRPass` 边被**错误生成**；
   - 拓扑序变成 `LightingPass → ApplyReflection → … → SSRPass → SSRBlur → SSRTemporal`。
4. **ApplyReflection 跑到整个 SSR 链之前**，采样还没写入的 `SSR_Temporal`（undefined / 陈旧内容）→ 轮廓黑圈。

**为什么「一开始就开 SSR」不触发**：全部 pass 首帧即 active，没有 culled pass 触发重排，`m_Passes` 保持声明顺序，边正确。

---

## 4. 修复

把「声明顺序」和「执行顺序」分离：

```cpp
// RenderGraph.hpp
std::vector<std::shared_ptr<RGPass>> m_Passes;          // 声明顺序（AddPass 顺序），永不重排
std::vector<std::shared_ptr<RGPass>> m_ExecutionOrder;  // 拓扑执行顺序（Compile 里算）
```

```cpp
// Compile() 里：不再写回 m_Passes，只写执行顺序
m_ExecutionOrder = std::move(sorted);

// Execute() 里：遍历执行顺序
for (auto& pass : m_ExecutionOrder) { ... }
```

配套改动：
- `Clear()` / `ExtractState()` 清空 `m_ExecutionOrder`；
- `RestoreState()` 清空 `m_ExecutionOrder` 并置 `m_Compiled = false`（因为执行顺序没进快照，回滚后必须重编译）。

---

## 5. 通用教训（后续加 pass 必读）

1. **RenderGraph 的 producer 映射依赖「声明顺序」**。任何代码都不能在 `Compile()` 之后重排 `m_Passes`。执行顺序要单独存（`m_ExecutionOrder`）。
2. **运行时 un-cull 一个 pass ≠ 重新 AddPass**。`ApplyPerFrameCulling` 只翻转 `IsCulled`；pass 的 `TextureReads/TextureWrites/WriteLoadOps/DepthReadTextures` 在 `AddPass` 时就固定了。若新 pass 依赖其它 pass 的输出，必须保证声明顺序正确。
3. **新增 pass 时，把它的 `AddPass` 放在它依赖的 producer 之后**（即「声明顺序 = 粗略拓扑序」），否则即便不 un-cull 也可能依赖边算错。
4. **「整图 Clear+AddPass 能修」是一个强信号**：它说明脏状态在 `RGPass`/`RGTexture` 对象本身（顺序、声明、标志位），而不是 FBO 内容或 layout。遇到这种「重建就好了」的现象，优先查对象级状态。

---

## 6. 快速自查清单（遇到「运行时开 pass 出问题」时）

1. 确认「一开始就开」是否正常 —— 是 → 基本锁定「运行时 un-cull」的状态问题。
2. 确认「移动相机/物体」是否暂时正常 —— 是 → 依赖边/时序相关的错序。
3. 看 FrameDebugger 里各 pass 输出，找到黑/脏内容**最早出现在哪个 pass**，缩小范围。
4. 二分：把新 pass 的输入/输出逐个「强制为合法值」，定位是哪个依赖边断了。
5. 检查 `m_Passes` 是否在 `Compile()` 之外被重排（这是本 bug 的根因，也是最高频的坑）。

---

## 7. 附：本次顺带修掉的 SSR 独立小问题

这些和黑圈无关，但排查过程中一并修了，记录备查：

- **时序历史未重置**：`VulkanSSRTemporalPass::ResetHistory()`，在 `ApplyPerFrameCulling` 里 `!enableSSR` 时调用。
- **第一帧错位重投影**：`HasHistory` push constant，第一帧直接输出当前帧 SSR。
- **预乘 alpha clamp 不一致**：neighborhood clamp 同时 clamp `rgb` 和 `alpha`。
- **天空反射发黑**：`ssr_march.frag` 二分细化后若命中天空，按 miss 回退 IBL。
- **SRP 缺 SceneDepth**：`assets/Editor/srp/default.srp` 补 `SceneDepth` + `DepthPrePass`。

（详见 `VULKAN_ARCHITECTURE_REPORT.md` §2 / §14.8 / §15）
