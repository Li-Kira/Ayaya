#include "ayapch.h"
#include "VulkanRenderCommandBuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Platform/Vulkan/VulkanTexture2D.hpp"
#include "Platform/Vulkan/VulkanTextureCube.hpp"
#include "Platform/Vulkan/VulkanBuffer.hpp" // 【新增】
#include "Core/Application.hpp"
#include "Renderer/Mesh.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    VulkanRenderCommandBuffer::VulkanRenderCommandBuffer() {}
    VulkanRenderCommandBuffer::~VulkanRenderCommandBuffer() {}

    void VulkanRenderCommandBuffer::Begin() {}
    void VulkanRenderCommandBuffer::End() {}

    void VulkanRenderCommandBuffer::BeginRenderPass(const std::shared_ptr<Framebuffer>& targetFBO, bool clear, const glm::vec4& clearColor) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmdBuffer = context->GetCurrentCommandBuffer();
        
        auto vulkanFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(targetFBO);
        if (!vulkanFBO) return;

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = vulkanFBO->GetVulkanRenderPass();
        renderPassInfo.framebuffer = vulkanFBO->GetVulkanFramebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = { vulkanFBO->GetSpecification().Width, vulkanFBO->GetSpecification().Height };

        std::vector<VkClearValue> clearValues;
        for (auto format : vulkanFBO->GetSpecification().Attachments.Attachments) {
            VkClearValue clearVal{};
            if (format.TextureFormat == FramebufferTextureFormat::DEPTH24STENCIL8 || format.TextureFormat == FramebufferTextureFormat::Depth) {
                clearVal.depthStencil = {1.0f, 0};
            } else {
                clearVal.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
            }
            clearValues.push_back(clearVal);
        }
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // ==========================================
        // 【核心修复 1】：Vulkan 负高度视口 (Y轴翻转黑魔法)
        // 完美解决由于复用 OpenGL 投影矩阵导致的上下颠倒和背面剔除错误！
        // ==========================================
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = (float)vulkanFBO->GetSpecification().Height; // 起点移到下方
        viewport.width = (float)vulkanFBO->GetSpecification().Width;
        viewport.height = -(float)vulkanFBO->GetSpecification().Height; // 负数高度向上拉！
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = { vulkanFBO->GetSpecification().Width, vulkanFBO->GetSpecification().Height };
        vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
    }

    void VulkanRenderCommandBuffer::EndRenderPass() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        vkCmdEndRenderPass(context->GetCurrentCommandBuffer());
    }

   void VulkanRenderCommandBuffer::BindPipeline(const std::shared_ptr<Pipeline>& pipeline) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        
        if (vulkanPipeline && vulkanPipeline->GetVulkanPipeline() != VK_NULL_HANDLE) {
            VkCommandBuffer cmdBuffer = context->GetCurrentCommandBuffer();
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipeline());

            // 绑定 UBO (Set 0) — 使用当前帧索引避免三重缓冲数据错乱
            uint32_t frameIndex = context->GetCurrentFrameIndex() % 3;
            VkDescriptorSet set0 = vulkanPipeline->GetVulkanDescriptorSet(0, frameIndex);
            if (set0 != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipelineLayout(), 0, 1, &set0, 0, nullptr);
            }

            // 【核心】：切换管线时，清空之前记的小本本，并记住新管线
            m_BoundPipeline = vulkanPipeline;
            m_PendingImageInfos.clear();
            m_DescriptorSetDirty = false;
        }
    }

    void VulkanRenderCommandBuffer::PushConstantData(const std::shared_ptr<Pipeline>& pipeline, const void* data, uint32_t size) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        if (!vulkanPipeline) return;

        vkCmdPushConstants(context->GetCurrentCommandBuffer(), 
                           vulkanPipeline->GetVulkanPipelineLayout(), 
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                           0, size, data);
    }

    // ==========================================
    // 绑定贴图：只记小本本，不跟 Vulkan 通信！
    // ==========================================
    // 1. Texture2D 绑定
    void VulkanRenderCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Texture2D>& texture) {
        auto vulkanTexture = std::dynamic_pointer_cast<VulkanTexture2D>(texture);
        if (!vulkanTexture || vulkanTexture->GetImageView() == VK_NULL_HANDLE) return;
        
        // ==========================================
        // 【核心修复：状态缓存拦截】
        // 如果这个槽位之前绑定的贴图和现在要绑的一模一样，直接退出！
        // 绝不触发 Dirty，绝不浪费新的 Descriptor Set！
        // ==========================================
        if (m_PendingImageInfos.find(slot) != m_PendingImageInfos.end()) {
            if (m_PendingImageInfos[slot].imageView == vulkanTexture->GetImageView()) return;
        }
        
        m_PendingImageInfos[slot] = { vulkanTexture->GetSampler(), vulkanTexture->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        m_DescriptorSetDirty = true; 
    }

    // 2. Framebuffer 附件绑定
    void VulkanRenderCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Framebuffer>& framebuffer, uint32_t attachmentIndex, bool isDepth) {
        auto vulkanFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(framebuffer);
        if (!vulkanFBO) return;
        VkImageView view = isDepth ? vulkanFBO->GetDepthAttachmentImageView() : vulkanFBO->GetColorAttachmentImageView(attachmentIndex);
        if (view == VK_NULL_HANDLE) return;

        // 【拦截检查】
        if (m_PendingImageInfos.find(slot) != m_PendingImageInfos.end()) {
            if (m_PendingImageInfos[slot].imageView == view) return;
        }

        // 深度附件使用 DEPTH_STENCIL_READ_ONLY_OPTIMAL，颜色附件使用 SHADER_READ_ONLY_OPTIMAL
        VkImageLayout layout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        m_PendingImageInfos[slot] = { vulkanFBO->GetSampler(), view, layout };
        m_DescriptorSetDirty = true;
    }

    // 3. TextureCube 绑定
    void VulkanRenderCommandBuffer::BindTextureCube(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<TextureCube>& texture) {
        auto vulkanTexture = std::dynamic_pointer_cast<VulkanTextureCube>(texture);
        if (!vulkanTexture || vulkanTexture->GetImageView() == VK_NULL_HANDLE) return;
        
        // 【拦截检查】
        if (m_PendingImageInfos.find(slot) != m_PendingImageInfos.end()) {
            if (m_PendingImageInfos[slot].imageView == vulkanTexture->GetImageView()) return;
        }

        m_PendingImageInfos[slot] = { vulkanTexture->GetSampler(), vulkanTexture->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        m_DescriptorSetDirty = true; 
    }

    // ==========================================
    // 核心冲刷器：Draw 之前的终极发车！
    // ==========================================
    void VulkanRenderCommandBuffer::FlushDescriptorSets() {
        // 【核心防御 1】：如果没绑管线、没贴图、或者贴图压根没换过，直接返回！
        // 显卡会继续沿用上一次绑定的 Descriptor Set，既安全又省性能！
        if (!m_BoundPipeline || m_PendingImageInfos.empty() || !m_DescriptorSetDirty) return;

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();

        VkDescriptorSet newSet = m_BoundPipeline->GetNextTextureDescriptorSet();
        AYAYA_CORE_ASSERT(newSet != VK_NULL_HANDLE, "Failed to get Descriptor Set!");

        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(m_PendingImageInfos.size());

        // 这里会把当前积累的【所有】有效贴图（包括上一个物体遗留的 IBL 环境光）一并写入新 Set
        for (auto& pair : m_PendingImageInfos) {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = newSet;
            write.dstBinding = pair.first;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &pair.second;
            writes.push_back(write);
        }

        vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
        vkCmdBindDescriptorSets(context->GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_BoundPipeline->GetVulkanPipelineLayout(), 1, 1, &newSet, 0, nullptr);

        // ==========================================
        // 【终极修复】：打扫战场
        // m_PendingImageInfos.clear(); <--- 彻底删掉这句罪魁祸首！绝对不能清空！
        // ==========================================
        
        // 只重置脏标记，代表当前的配置已经发车完毕
        m_DescriptorSetDirty = false; 
    }

    // --- 绘制指令 ---
    void VulkanRenderCommandBuffer::DrawIndexed(const std::shared_ptr<Mesh>& mesh, uint32_t indexCount) {
        if (!m_BoundPipeline) return;
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        uint32_t currentFrame = context->GetCurrentFrameIndex() % 3;
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();

        // 1. 【核心修复】：绑定相机数据 (Set 0) — 使用当前帧索引
        VkDescriptorSet cameraSet = m_BoundPipeline->GetVulkanDescriptorSet(0, currentFrame);
        if (cameraSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                    m_BoundPipeline->GetVulkanPipelineLayout(), 0, 1, &cameraSet, 0, nullptr);
        }

        // 2. 绑定材质贴图 (Set 1)
        FlushDescriptorSets(); 

        // 3. 【核心修复】：绑定 VBO 和 IBO
        auto vulkanVB = std::dynamic_pointer_cast<VulkanVertexBuffer>(mesh->GetVertexBuffer());
        auto vulkanIB = std::dynamic_pointer_cast<VulkanIndexBuffer>(mesh->GetIndexBuffer());
        if (vulkanVB && vulkanIB) {
            VkBuffer vbs[] = { vulkanVB->GetVulkanBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);
            vkCmdBindIndexBuffer(cmd, vulkanIB->GetVulkanBuffer(), 0, VK_INDEX_TYPE_UINT32);
        }

        // 4. 执行绘制
        uint32_t count = indexCount ? indexCount : mesh->GetIndexCount();
        vkCmdDrawIndexed(cmd, count, 1, 0, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawArrays(uint32_t vertexCount) {
        if (!m_BoundPipeline) return;
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();
        uint32_t frameIndex = context->GetCurrentFrameIndex() % 3;

        // 必须重新绑定 Set 0，确保 UBO 数据对应当前帧
        VkDescriptorSet cameraSet = m_BoundPipeline->GetVulkanDescriptorSet(0, frameIndex);
        if (cameraSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_BoundPipeline->GetVulkanPipelineLayout(), 0, 1, &cameraSet, 0, nullptr);
        }
        FlushDescriptorSets();
        vkCmdDraw(cmd, vertexCount, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawArrays(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount) {
        if (!m_BoundPipeline) return;
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();
        uint32_t frameIndex = context->GetCurrentFrameIndex() % 3;

        VkDescriptorSet cameraSet = m_BoundPipeline->GetVulkanDescriptorSet(0, frameIndex);
        if (cameraSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_BoundPipeline->GetVulkanPipelineLayout(), 0, 1, &cameraSet, 0, nullptr);
        }
        FlushDescriptorSets();
        uint32_t count = vertexCount ? vertexCount : mesh->GetVertexCount();
        vkCmdDraw(cmd, count, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawTriangleStrip(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount) {
        if (!m_BoundPipeline) return;
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();
        uint32_t frameIndex = context->GetCurrentFrameIndex() % 3;

        VkDescriptorSet cameraSet = m_BoundPipeline->GetVulkanDescriptorSet(0, frameIndex);
        if (cameraSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_BoundPipeline->GetVulkanPipelineLayout(), 0, 1, &cameraSet, 0, nullptr);
        }
        FlushDescriptorSets();
        uint32_t count = vertexCount ? vertexCount : mesh->GetVertexCount();
        vkCmdDraw(cmd, count, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawTriangleStrip(uint32_t vertexCount) {
        if (!m_BoundPipeline) return;
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();
        uint32_t frameIndex = context->GetCurrentFrameIndex() % 3;

        VkDescriptorSet cameraSet = m_BoundPipeline->GetVulkanDescriptorSet(0, frameIndex);
        if (cameraSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_BoundPipeline->GetVulkanPipelineLayout(), 0, 1, &cameraSet, 0, nullptr);
        }
        FlushDescriptorSets();
        vkCmdDraw(cmd, vertexCount, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawArrays(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) {
        if (!m_BoundPipeline) return;

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();

        // ==========================================
        // 1. 【核心修复】：必须绑定 Set 0！
        // 哪怕后处理 Shader 里没用到，Vulkan 也要求布局槽位必须对齐填满！
        // ==========================================
        uint32_t frameIndex = context->GetCurrentFrameIndex() % 3;
        VkDescriptorSet cameraSet = m_BoundPipeline->GetVulkanDescriptorSet(0, frameIndex);
        if (cameraSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                    m_BoundPipeline->GetVulkanPipelineLayout(), 0, 1, &cameraSet, 0, nullptr);
        }

        // 2. 绑定贴图 (Set 1)
        FlushDescriptorSets(); 

        // ==========================================
        // 3. 【核心逻辑】：智能空判断
        // 如果 VAO 里真的有 VBO，就绑定它；
        // 如果是 m_EmptyVAO（空的），直接跳过绑定，进入无图元绘制模式！
        // ==========================================
        if (vertexArray && !vertexArray->GetVertexBuffers().empty()) {
            auto vulkanVB = std::dynamic_pointer_cast<VulkanVertexBuffer>(vertexArray->GetVertexBuffers()[0]);
            if (vulkanVB) {
                VkBuffer vbs[] = { vulkanVB->GetVulkanBuffer() };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);
            }
        }

        // 4. 发起绘制！
        uint32_t count = vertexCount ? vertexCount : 3;
        vkCmdDraw(cmd, count, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::InsertExecutionBarrier() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmdBuffer = context->GetCurrentCommandBuffer();

        VkMemoryBarrier memoryBarrier{};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(
            cmdBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            1, &memoryBarrier,
            0, nullptr,
            0, nullptr
        );
    }

}