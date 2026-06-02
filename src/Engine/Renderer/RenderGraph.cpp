#include "ayapch.h"
#include "RenderGraph.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include <vulkan/vulkan.h>
#include <queue>
#include <unordered_set>
#include <algorithm>

namespace Ayaya {

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

    void RenderGraph::Clear() { m_Passes.clear(); m_Textures.clear(); m_Compiled = false; }

    RGTexture& RenderGraph::RegisterTexture(const std::string& name, const FramebufferSpecification& spec) {
        auto it = m_Textures.find(name);
        if (it != m_Textures.end()) { return it->second; }
        RGTexture tex; tex.Name = name; tex.Spec = spec;
        m_Textures[name] = tex; return m_Textures[name];
    }
    RGTexture* RenderGraph::GetTexture(const std::string& name) {
        auto it = m_Textures.find(name);
        return it != m_Textures.end() ? &it->second : nullptr;
    }

    void RenderGraph::Compile() {
        if (m_Passes.empty()) return;
        std::unordered_map<std::string, std::string> producers;
        for (auto& p : m_Passes) for (auto& w : p->TextureWrites) producers[w] = p->Name;
        std::unordered_map<std::string, std::vector<std::string>> edges;
        std::unordered_map<std::string, int> deg;
        for (auto& p : m_Passes) { deg[p->Name] = 0; }
        for (auto& p : m_Passes) for (auto& r : p->TextureReads) {
            auto it = producers.find(r);
            if (it != producers.end() && it->second != p->Name) { edges[it->second].push_back(p->Name); deg[p->Name]++; }
        }
        std::queue<std::string> q;
        for (auto& [n,d] : deg) if (d==0) q.push(n);
        std::vector<std::shared_ptr<RGPass>> sorted;
        while (!q.empty()) {
            auto c = q.front(); q.pop();
            auto it = std::find_if(m_Passes.begin(),m_Passes.end(),[&](auto&p){return p->Name==c;});
            if (it!=m_Passes.end()) sorted.push_back(*it);
            for (auto& nb : edges[c]) if (--deg[nb]==0) q.push(nb);
        }
        m_Passes = std::move(sorted);
        for (auto& [n,t] : m_Textures)
            if (!t.PhysicalFBO) t.PhysicalFBO = Framebuffer::Create(t.Spec);
        m_Compiled = true;
    }

    void RenderGraph::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        if (!m_Compiled) Compile();

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());

        for (auto& pass : m_Passes) {
            if (pass->IsCulled) continue;
            for (auto& w : pass->TextureWrites) {
                auto it = m_Textures.find(w);
                if (it != m_Textures.end() && it->second.PhysicalFBO)
                    context.Framebuffers[w] = it->second.PhysicalFBO;
            }
            for (auto& r : pass->TextureReads) {
                auto it = m_Textures.find(r);
                if (it != m_Textures.end() && it->second.PhysicalFBO)
                    context.Framebuffers[r] = it->second.PhysicalFBO;
            }
            pass->ExecuteCallback(context, cmd);

            // 【TBDR Fix】每个 Pass 执行后立即插入 tile resolve barrier，
            // 确保当前 Pass 的 tile writes 在下一个 Pass 读取前已刷新到 system memory
            if (vkCtx) {
                for (auto& w : pass->TextureWrites) {
                    auto it = m_Textures.find(w);
                    if (it == m_Textures.end() || !it->second.PhysicalFBO) continue;
                    auto vkFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(it->second.PhysicalFBO);
                    if (!vkFBO) continue;

                    bool isDepthOnly = true;
                    for (auto& att : it->second.Spec.Attachments.Attachments) {
                        if (att.TextureFormat != FramebufferTextureFormat::Depth &&
                            att.TextureFormat != FramebufferTextureFormat::DEPTH24STENCIL8)
                            { isDepthOnly = false; break; }
                    }

                    VkImage image = isDepthOnly
                        ? vkFBO->GetDepthAttachmentImage()
                        : vkFBO->GetColorAttachmentImage(0);
                    if (image == VK_NULL_HANDLE) continue;

                    VkImageLayout layout = isDepthOnly
                        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    VkImageMemoryBarrier resolveBarrier{};
                    resolveBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    resolveBarrier.oldLayout = layout;
                    resolveBarrier.newLayout = layout;
                    resolveBarrier.srcAccessMask = isDepthOnly
                        ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                        : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    resolveBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    resolveBarrier.image = image;
                    resolveBarrier.subresourceRange.aspectMask = isDepthOnly ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
                    resolveBarrier.subresourceRange.levelCount = 1;
                    resolveBarrier.subresourceRange.layerCount = 1;

                    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    srcStage |= isDepthOnly
                        ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT   // 深度写入在此阶段
                        : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

                    vkCmdPipelineBarrier(vkCtx->GetCurrentCommandBuffer(),
                        srcStage,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0,
                        0, nullptr, 0, nullptr, 1, &resolveBarrier);
                }
            }
        }

        cmd.InsertExecutionBarrier();
    }

    std::shared_ptr<Framebuffer> RenderGraph::GetPhysicalFBO(const std::string& name, uint32_t) {
        auto it = m_Textures.find(name);
        return it != m_Textures.end() ? it->second.PhysicalFBO : nullptr;
    }
}
