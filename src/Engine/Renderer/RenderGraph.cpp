#include "ayapch.h"
#include "RenderGraph.hpp"
#include "Core/Log.hpp"

#include <queue>
#include <unordered_set>
#include <algorithm>

namespace Ayaya {

    // ==========================================
    // 1. RGBuilder 实现 (Setup 阶段收集依赖)
    // ==========================================

    RGBuilder::RGBuilder(RenderGraph& graph, RGPass& pass)
        : m_Graph(graph), m_Pass(pass) {}

    void RGBuilder::ReadTexture(const std::string& name) {
        m_Pass.TextureReads.push_back(name);
        auto* tex = m_Graph.GetTexture(name);
        if (tex) tex->IsRead = true;
    }

    void RGBuilder::WriteTexture(const std::string& name, const FramebufferSpecification& spec) {
        m_Pass.TextureWrites.push_back(name);
        auto& tex = m_Graph.RegisterTexture(name, spec);
        tex.IsWritten = true;

        if (name == "FinalOutput" || name == "Swapchain" || name == "UI") {
            m_Pass.HasSideEffect = true;
        }
    }

    // ==========================================
    // 2. RenderGraph 资源管理
    // ==========================================

    void RenderGraph::Clear() {
        m_Passes.clear();
        m_Textures.clear();
        m_Compiled = false;
    }

    RGTexture& RenderGraph::RegisterTexture(const std::string& name, const FramebufferSpecification& spec) {
        auto it = m_Textures.find(name);
        if (it != m_Textures.end()) {
            it->second.Spec.Width  = std::max(it->second.Spec.Width,  spec.Width);
            it->second.Spec.Height = std::max(it->second.Spec.Height, spec.Height);
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
        if (it != m_Textures.end()) return &it->second;
        AYAYA_CORE_WARN("RenderGraph: Pass reads texture '{0}' which was never declared via WriteTexture.", name);
        return nullptr;
    }

    // ==========================================
    // 3. 核心大脑：图编译 (Compile)
    // ==========================================

    void RenderGraph::Compile() {
        if (m_Passes.empty()) return;

        // --- 阶段 A：构建生产者映射 (Who Writes What) ---
        std::unordered_map<std::string, std::string> textureProducers;
        for (const auto& pass : m_Passes) {
            for (const auto& writeTex : pass->TextureWrites) {
                textureProducers[writeTex] = pass->Name;
            }
        }

        // --- 阶段 B：构建邻接表与入度 ---
        std::unordered_map<std::string, std::vector<std::string>> passEdges;
        std::unordered_map<std::string, int> inDegree;

        for (const auto& pass : m_Passes) {
            inDegree[pass->Name] = 0;
        }

        for (const auto& pass : m_Passes) {
            for (const auto& readTex : pass->TextureReads) {
                auto prodIt = textureProducers.find(readTex);
                if (prodIt != textureProducers.end()) {
                    const std::string& producer = prodIt->second;
                    if (producer != pass->Name) {
                        passEdges[producer].push_back(pass->Name);
                        inDegree[pass->Name]++;
                    }
                } else {
                    AYAYA_CORE_WARN("RenderGraph: Pass '{0}' reads '{1}' — no producer in graph (external resource?).",
                        pass->Name, readTex);
                }
            }
        }

        // --- 阶段 C：Kahn 拓扑排序 ---
        std::queue<std::string> zeroQueue;
        for (const auto& [name, degree] : inDegree) {
            if (degree == 0) zeroQueue.push(name);
        }

        std::vector<std::shared_ptr<RGPass>> sorted;
        while (!zeroQueue.empty()) {
            std::string current = zeroQueue.front();
            zeroQueue.pop();

            auto it = std::find_if(m_Passes.begin(), m_Passes.end(),
                [&](const auto& p) { return p->Name == current; });
            if (it != m_Passes.end()) sorted.push_back(*it);

            for (const auto& neighbor : passEdges[current]) {
                if (--inDegree[neighbor] == 0) {
                    zeroQueue.push(neighbor);
                }
            }
        }

        // 循环依赖检测
        if (sorted.size() != m_Passes.size()) {
            AYAYA_CORE_ERROR("RenderGraph Compile Failed: Circular dependency detected!");
            for (const auto& pass : m_Passes) {
                if (std::find_if(sorted.begin(), sorted.end(),
                        [&](const auto& s) { return s->Name == pass->Name; }) == sorted.end()) {
                    AYAYA_CORE_ERROR("  Unresolved pass: {0}", pass->Name);
                }
            }
            AYAYA_CORE_ASSERT(false, "Circular Dependency in Render Graph!");
            return;
        }

        m_Passes = std::move(sorted);

        // --- 阶段 D：物理资源分配 (3 帧飞行) ---
        for (auto& [name, tex] : m_Textures) {
            if (name == "Swapchain") continue;
            for (int i = 0; i < 3; i++) {
                if (!tex.PhysicalFBO[i]) {
                    tex.PhysicalFBO[i] = Framebuffer::Create(tex.Spec);
                    tex.IsNew[i] = true;
                }
            }
        }

        m_Compiled = true;

        AYAYA_CORE_INFO("=== Render Graph Compiled ({0} passes) ===", m_Passes.size());
        for (size_t i = 0; i < m_Passes.size(); i++) {
            std::string reads;
            for (auto& r : m_Passes[i]->TextureReads) { reads += r + " "; }
            std::string writes;
            for (auto& w : m_Passes[i]->TextureWrites) { writes += w + " "; }
            AYAYA_CORE_INFO("  [{0}] {1}  READ=[{2}] WRITE=[{3}]",
                i, m_Passes[i]->Name, reads.empty() ? "-" : reads, writes.empty() ? "-" : writes);
        }
    }

    // ==========================================
    // 4. 执行管线 (Execute) — 带自动 Barrier 插入
    // ==========================================

    void RenderGraph::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        if (!m_Compiled) Compile();

        uint32_t frameIdx = context.Get<uint32_t>("CurrentFrameIndex", 0) % 3;

        for (auto& pass : m_Passes) {
            if (pass->IsCulled) continue;

            // 1. 写入资源
            for (const auto& writeTexName : pass->TextureWrites) {
                auto it = m_Textures.find(writeTexName);
                if (it == m_Textures.end() || !it->second.PhysicalFBO[frameIdx]) continue;
                auto& tex = it->second;

                if (tex.IsNew[frameIdx]) {
                    cmd.TransitionImageLayout(tex.PhysicalFBO[frameIdx], 0,
                        ImageLayout::Undefined, ImageLayout::ShaderReadOnlyOptimal);
                    tex.IsNew[frameIdx] = false;
                }
                tex.CurrentLayout[frameIdx] = ImageLayout::ShaderReadOnlyOptimal;
                context.Framebuffers[writeTexName] = tex.PhysicalFBO[frameIdx];
            }

            // 2. 读取资源
            for (const auto& readTexName : pass->TextureReads) {
                auto it = m_Textures.find(readTexName);
                if (it == m_Textures.end() || !it->second.PhysicalFBO[frameIdx]) continue;
                auto& tex = it->second;

                if (tex.CurrentLayout[frameIdx] != ImageLayout::ShaderReadOnlyOptimal) {
                    cmd.TransitionImageLayout(tex.PhysicalFBO[frameIdx], 0,
                        tex.CurrentLayout[frameIdx], ImageLayout::ShaderReadOnlyOptimal);
                    tex.CurrentLayout[frameIdx] = ImageLayout::ShaderReadOnlyOptimal;
                }
                context.Framebuffers[readTexName] = tex.PhysicalFBO[frameIdx];
            }

            // 3. 执行渲染
            pass->ExecuteCallback(context, cmd);
        }
    }

    std::shared_ptr<Framebuffer> RenderGraph::GetPhysicalFBO(const std::string& name, uint32_t frameIndex) {
        auto it = m_Textures.find(name);
        if (it != m_Textures.end()) return it->second.PhysicalFBO[frameIndex % 3];
        return nullptr;
    }

} // namespace Ayaya
