#include "ayapch.h"
#include "RenderGraph.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include <vulkan/vulkan.h>
#include <queue>
#include <algorithm>

namespace Ayaya {

    // ==========================================
    // Helper — 纯深度纹理判断
    // ==========================================
    static bool IsDepthOnlyTexture(const FramebufferSpecification& spec) {
        for (auto& att : spec.Attachments.Attachments) {
            if (att.TextureFormat != FramebufferTextureFormat::Depth &&
                att.TextureFormat != FramebufferTextureFormat::DEPTH24STENCIL8) {
                return false;
            }
        }
        return true;
    }

    // ==========================================
    // RGBuilder — DSL 实现
    // ==========================================
    RGBuilder::RGBuilder(RenderGraph& graph, RGPass& pass) : m_Graph(graph), m_Pass(pass) {}

    void RGBuilder::ReadTexture(const std::string& name) {
        m_Pass.TextureReads.push_back(name);
        auto* tex = m_Graph.GetTexture(name);
        if (tex) tex->IsRead = true;
    }

    void RGBuilder::WriteTexture(const std::string& name, const FramebufferSpecification& spec) {
        m_Pass.TextureWrites.push_back(name);
        auto& tex = m_Graph.RegisterTexture(name, spec);
        tex.IsWritten = true;
    }

    void RGBuilder::ReadWriteTexture(const std::string& name, const FramebufferSpecification& spec) {
        ReadTexture(name);
        WriteTexture(name, spec);
    }

    // ==========================================
    // 纹理注册
    // ==========================================
    void RenderGraph::Clear() {
        m_Passes.clear();
        m_Textures.clear();
        m_Compiled = false;
    }

    RGTexture& RenderGraph::RegisterTexture(const std::string& name, const FramebufferSpecification& spec) {
        auto it = m_Textures.find(name);
        if (it != m_Textures.end()) {
            const auto& existing = it->second.Spec;
            bool same = (existing.Width == spec.Width && existing.Height == spec.Height
                      && existing.Samples == spec.Samples
                      && existing.Attachments.Attachments.size() == spec.Attachments.Attachments.size());
            if (!same) {
                AYAYA_CORE_WARN("[RenderGraph] Texture '{}' registered with conflicting specs", name);
            }
            return it->second;
        }
        RGTexture tex;
        tex.Name = name;
        tex.Spec = spec;
        m_Textures[name] = tex;
        return m_Textures[name];
    }

    RGTexture* RenderGraph::GetTexture(const std::string& name) {
        auto it = m_Textures.find(name);
        return it != m_Textures.end() ? &it->second : nullptr;
    }

    // ==========================================
    // GetReadLayout — 判断纹理被采样时应处于的布局
    // ==========================================
    ImageLayout RenderGraph::GetReadLayout(const FramebufferSpecification& spec) {
        if (IsDepthOnlyTexture(spec))
            return ImageLayout::DepthStencilReadOnlyOptimal;
        return ImageLayout::ShaderReadOnlyOptimal;
    }

    // ==========================================
    // EnsureReadable — 确保纹理可被 shader 采样
    //
    // 动态渲染的 vkCmdEndRendering 后图像留在 attachment 布局，
    // 而 InsertTileResolveBarrier 已将其转为 SHADER_READ_ONLY（或 DEPTH_STENCIL_READ_ONLY）。
    // 正常情况下后续 Pass 读取时 CurrentLayout 已是 ShaderReadOnlyOptimal → 无需 barrier。
    //
    // 此函数处理首次读取（UNDEFINED → SHADER_READ_ONLY）及异常情况。
    // ==========================================
    void RenderGraph::EnsureReadable(RGTexture& tex, uint32_t frameIndex, RenderCommandBuffer& cmd) {
        uint32_t idx = frameIndex % kRenderGraphFramesInFlight;
        ImageLayout current = tex.CurrentLayout[idx];
        ImageLayout target  = GetReadLayout(tex.Spec);

        if (current == target) return;  // 已在可读布局

        // 首次使用：布局从 UNDEFINED 转为可读
        // VulkanFramebuffer 在创建时已用 single-time command 做了此转换，
        // 此处作为安全兜底显式插入 layout transition barrier
        if (tex.PhysicalFBOs[idx]) {
            cmd.TransitionImageLayout(tex.PhysicalFBOs[idx], 0, current, target);
        }
        tex.CurrentLayout[idx] = target;
    }

    // ==========================================
    // Compile — Kahn 拓扑排序 + FBO 创建
    // ==========================================
    void RenderGraph::Compile() {
        if (m_Passes.empty()) return;

        // Step 1: 建立生产者映射 + 隐式多生产者链
        //   - 首次 Write: 注册为生产者
        //   - 重复 Write (ReadWrite / 累加): 记录隐式依赖 → 后写入者依赖先写入者
        std::unordered_map<std::string, std::string> producers;
        std::vector<std::pair<std::string, std::string>> implicitEdges; // (from, to)
        for (auto& p : m_Passes) {
            for (auto& w : p->TextureWrites) {
                auto it = producers.find(w);
                if (it != producers.end() && it->second != p->Name) {
                    AYAYA_CORE_WARN("[RenderGraph] Texture '{}' written by '{}' and '{}' — adding implicit dependency",
                        w, it->second, p->Name);
                    implicitEdges.push_back({it->second, p->Name});
                }
                producers[w] = p->Name;
            }
        }

        // Step 2: 构建 DAG 边缘和入度
        std::unordered_map<std::string, std::vector<std::string>> edges;
        std::unordered_map<std::string, int> deg;
        for (auto& p : m_Passes) deg[p->Name] = 0;

        for (auto& p : m_Passes) {
            for (auto& r : p->TextureReads) {
                auto it = producers.find(r);
                if (it != producers.end() && it->second != p->Name) {
                    edges[it->second].push_back(p->Name);
                    deg[p->Name]++;
                }
            }
        }

        // 隐式多生产者边：先写入者 → 后写入者（ReadWrite / 累加渲染）
        for (auto& [from, to] : implicitEdges) {
            edges[from].push_back(to);
            deg[to]++;
        }

        // Step 3: Kahn 拓扑排序
        std::queue<std::string> q;
        for (auto& [name, d] : deg) {
            if (d == 0) q.push(name);
        }

        std::vector<std::shared_ptr<RGPass>> sorted;
        while (!q.empty()) {
            auto current = q.front(); q.pop();
            auto it = std::find_if(m_Passes.begin(), m_Passes.end(),
                [&](auto& p) { return p->Name == current; });
            if (it != m_Passes.end()) sorted.push_back(*it);

            for (auto& neighbor : edges[current]) {
                if (--deg[neighbor] == 0) q.push(neighbor);
            }
        }

        // Step 4: 环检测
        if (sorted.size() != m_Passes.size()) {
            AYAYA_CORE_ERROR("[RenderGraph] Circular dependency detected! {} passes unreachable",
                m_Passes.size() - sorted.size());
            for (auto& p : m_Passes) {
                if (std::find_if(sorted.begin(), sorted.end(),
                        [&](auto& s) { return s->Name == p->Name; }) == sorted.end()) {
                    sorted.push_back(p);
                }
            }
        }

        m_Passes = std::move(sorted);

        // Step 5: 为每个纹理创建 3 帧缓冲物理 FBO
        // VulkanFramebuffer::Invalidate 的初始过渡：颜色→SHADER_READ_ONLY，深度→DEPTH_ATTACHMENT
        for (auto& [name, tex] : m_Textures) {
            bool depthOnly = true;
            for (auto& att : tex.Spec.Attachments.Attachments) {
                if (att.TextureFormat != FramebufferTextureFormat::Depth &&
                    att.TextureFormat != FramebufferTextureFormat::DEPTH24STENCIL8) {
                    depthOnly = false; break;
                }
            }
            ImageLayout initLayout = depthOnly
                ? ImageLayout::DepthStencilAttachmentOptimal
                : ImageLayout::ShaderReadOnlyOptimal;

            for (uint32_t i = 0; i < kRenderGraphFramesInFlight; ++i) {
                if (!tex.PhysicalFBOs[i]) {
                    tex.PhysicalFBOs[i] = Framebuffer::Create(tex.Spec);
                }
                tex.CurrentLayout[i] = initLayout;
            }
        }

        m_Compiled = true;
    }

    // ==========================================
    // EnsureWritable — 基于 CurrentLayout 追踪的精确布局转换
    //
    // 根据实时追踪的 CurrentLayout 决定是否需要 barrier：
    //   - 已在 attachment 布局 → 跳过（零开销）
    //   - 在只读布局 → 转为 attachment（保留内容，支持 LOAD 操作）
    //   - 在 UNDEFINED → 转为 attachment（首帧，无需保留内容）
    void RenderGraph::EnsureWritable(RGTexture& tex, uint32_t frameIndex, RenderCommandBuffer& cmd) {
        uint32_t idx = frameIndex % kRenderGraphFramesInFlight;
        if (!tex.PhysicalFBOs[idx]) return;

        bool isDepth = IsDepthOnlyTexture(tex.Spec);
        ImageLayout writeLayout = isDepth
            ? ImageLayout::DepthStencilAttachmentOptimal
            : ImageLayout::ColorAttachmentOptimal;
        ImageLayout current = tex.CurrentLayout[idx];

        if (current == writeLayout) return;

        // Transition ALL color attachments (or depth for depth-only)
        auto vkFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(tex.PhysicalFBOs[idx]);
        uint32_t count = isDepth ? 1 : (vkFBO ? vkFBO->GetColorAttachmentCount() : 1);
        for (uint32_t i = 0; i < count; i++)
            cmd.TransitionImageLayout(tex.PhysicalFBOs[idx], i, current, writeLayout);
        tex.CurrentLayout[idx] = writeLayout;
    }

    // ==========================================
    // Execute — 图执行入口（布局状态机）
    //
    // 动态渲染布局模型（Vulkan 1.3 Dynamic Rendering）：
    //   - vkCmdBeginRendering 不执行隐式布局转换 → 调用前图像必须已在 attachment 布局
    //   - vkCmdEndRendering 不执行隐式布局转换 → 调用后图像留在 attachment 布局
    //   - 所有布局转换由当前状态机显式管理
    //
    // 流程：
    //   1. 对每个 Pass（按拓扑序）：
    //      a. Write 纹理：EnsureWritable → 转为 attachment 布局（供 vkCmdBeginRendering 使用）
    //      b. Read 纹理：EnsureReadable → 转为 shader 可采样布局
    //      c. 注入当前帧 FBO 到 context 黑板
    //      d. 执行 Pass（vkCmdEndRendering 后图像留在 attachment 布局）
    //      e. InsertTileResolveBarrier → attachment→readOnly 布局转换 + TBDR tile sync
    //   2. 全局内存屏障
    // ==========================================
    void RenderGraph::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        if (!m_Compiled) Compile();

        uint32_t frameIndex = 0;
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (vkCtx) frameIndex = vkCtx->GetCurrentFrameIndex();
        uint32_t idx = frameIndex % kRenderGraphFramesInFlight;

        for (auto& pass : m_Passes) {
            if (pass->IsCulled) continue;

            // Step a: 确保 Write 纹理处于 attachment 布局（vkCmdBeginRendering 的前置条件）
            for (auto& w : pass->TextureWrites) {
                auto it = m_Textures.find(w);
                if (it != m_Textures.end() && it->second.PhysicalFBOs[idx])
                    EnsureWritable(it->second, frameIndex, cmd);
            }

            // Step b: 确保 Read 纹理处于可采样布局
            for (auto& r : pass->TextureReads) {
                auto it = m_Textures.find(r);
                if (it != m_Textures.end())
                    EnsureReadable(it->second, frameIndex, cmd);
            }

            // Step c: 注入当前帧 FBO 到 context 黑板
            //   Write 纹理 → 始终注入（Pass 需要渲染目标）
            for (auto& w : pass->TextureWrites) {
                auto it = m_Textures.find(w);
                if (it != m_Textures.end() && it->second.PhysicalFBOs[idx])
                    context.Framebuffers[w] = it->second.PhysicalFBOs[idx];
            }
            //   Read 纹理 → 仅在 context 中尚不存在时注入
            //   （允许前一 Pass 直接注入自己的 FBO，避免被空 FBO 覆盖）
            for (auto& r : pass->TextureReads) {
                auto it = m_Textures.find(r);
                if (it != m_Textures.end() && it->second.PhysicalFBOs[idx]) {
                    if (context.Framebuffers.find(r) == context.Framebuffers.end())
                        context.Framebuffers[r] = it->second.PhysicalFBOs[idx];
                }
            }

            // Step d: 执行 Pass。
            // vkCmdEndRendering 后图像留在 attachment 布局（COLOR_ATTACHMENT 或 DEPTH_STENCIL_ATTACHMENT），
            // CurrentLayout 由 EnsureWritable 已设置为对应的 attachment 布局。
            pass->ExecuteCallback(context, cmd);

            // Step e: Attachment → ReadOnly 布局转换 + TBDR tile-resolve
            // InsertTileResolveBarrier 负责：
            //   1) 将图像从 attachment 布局转为 shader 可采样布局（替代 VkRenderPass 的 finalLayout）
            //   2) 插入 execution+memory barrier 确保 TBDR on-chip tile 刷新到 system memory
            //   3) 更新 CurrentLayout 追踪
            for (auto& w : pass->TextureWrites) {
                auto it = m_Textures.find(w);
                if (it != m_Textures.end() && it->second.PhysicalFBOs[idx]) {
                    InsertTileResolveBarrier(it->second, frameIndex);
                }
            }
        }

        // 全局内存屏障：确保所有 Pass 的写入对后续 command（如 ImGui）可见
        cmd.InsertExecutionBarrier();
    }

    // ==========================================
    // InsertTileResolveBarrier — Attachment → ReadOnly 布局转换 + TBDR tile cache 刷新
    //
    // 动态渲染的 vkCmdEndRendering 不执行 finalLayout 转换（不像 VkRenderPass），
    // 因此需要显式 barrier 完成两件事：
    //   1) 布局转换：COLOR_ATTACHMENT → SHADER_READ_ONLY（或 DEPTH_ATTACHMENT → DEPTH_READ_ONLY）
    //   2) TBDR tile sync：execution+memory barrier 确保 on-chip tile 数据写入 system memory
    //
    // 转换完成后更新 CurrentLayout 追踪。
    // ==========================================
    void RenderGraph::InsertTileResolveBarrier(RGTexture& tex, uint32_t frameIndex) {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;

        uint32_t idx = frameIndex % kRenderGraphFramesInFlight;
        if (!tex.IsWritten || !tex.PhysicalFBOs[idx]) return;
        auto vkFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(tex.PhysicalFBOs[idx]);
        if (!vkFBO) return;

        bool isDepthOnly = IsDepthOnlyTexture(tex.Spec);

        // Transition ALL color attachments (or depth for depth-only textures)
        uint32_t count = isDepthOnly ? 1 : vkFBO->GetColorAttachmentCount();
        std::vector<VkImageMemoryBarrier> barriers(count);

        VkImageLayout oldLayout, newLayout;
        VkAccessFlags srcAccess, dstAccess;
        VkPipelineStageFlags srcStage;
        VkImageAspectFlags aspect;

        if (isDepthOnly) {
            oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dstAccess = VK_ACCESS_SHADER_READ_BIT;
            srcStage  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            aspect    = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            barriers[0].image = vkFBO->GetDepthAttachmentImage();
        } else {
            oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dstAccess = VK_ACCESS_SHADER_READ_BIT;
            srcStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            aspect    = VK_IMAGE_ASPECT_COLOR_BIT;
            for (uint32_t i = 0; i < count; i++)
                barriers[i].image = vkFBO->GetColorAttachmentImage(i);
        }

        for (uint32_t i = 0; i < count; i++) {
            if (barriers[i].image == VK_NULL_HANDLE) continue;
            barriers[i].sType     = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[i].oldLayout = oldLayout;
            barriers[i].newLayout = newLayout;
            barriers[i].srcAccessMask = srcAccess;
            barriers[i].dstAccessMask = dstAccess;
            barriers[i].subresourceRange.aspectMask = aspect;
            barriers[i].subresourceRange.levelCount = 1;
            barriers[i].subresourceRange.layerCount = 1;
        }

        vkCmdPipelineBarrier(vkCtx->GetCurrentCommandBuffer(),
            srcStage, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, count, barriers.data());

        tex.CurrentLayout[idx] = isDepthOnly
            ? ImageLayout::DepthStencilReadOnlyOptimal
            : ImageLayout::ShaderReadOnlyOptimal;
    }

    std::shared_ptr<Framebuffer> RenderGraph::GetPhysicalFBO(const std::string& name, uint32_t frameIndex) {
        auto it = m_Textures.find(name);
        if (it == m_Textures.end()) return nullptr;
        uint32_t idx = frameIndex % kRenderGraphFramesInFlight;
        return it->second.PhysicalFBOs[idx];
    }

} // namespace Ayaya
