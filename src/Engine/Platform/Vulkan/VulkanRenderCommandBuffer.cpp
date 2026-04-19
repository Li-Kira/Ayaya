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
        if (clear) {
            for (auto format : vulkanFBO->GetSpecification().Attachments.Attachments) {
                VkClearValue clearVal{};
                if (format.TextureFormat == FramebufferTextureFormat::DEPTH24STENCIL8 || format.TextureFormat == FramebufferTextureFormat::Depth) {
                    clearVal.depthStencil = {1.0f, 0};
                } else {
                    clearVal.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
                }
                clearValues.push_back(clearVal);
            }
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

            // 绑定 UBO (Set 0)
            VkDescriptorSet set0 = vulkanPipeline->GetVulkanDescriptorSet(0);
            if (set0 != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipelineLayout(), 0, 1, &set0, 0, nullptr);
            }

            // 【核心】：切换管线时，清空之前记的小本本，并记住新管线
            m_BoundPipeline = vulkanPipeline;
            m_PendingImageInfos.clear();
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
    // 在 VulkanRenderCommandBuffer.cpp 中
    void VulkanRenderCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Texture2D>& texture) {
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        auto vulkanTexture = std::dynamic_pointer_cast<VulkanTexture2D>(texture);
        
        if (!vulkanPipeline || !vulkanTexture) return;

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();

        // ==========================================
        // 【核心修复】：从管线的环形池中获取一个可用的 Set 1
        // ==========================================
        VkDescriptorSet textureSet = vulkanPipeline->GetNextTextureDescriptorSet();

        // 更新该描述符集，将其指向当前贴图的 View 和 Sampler
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = vulkanTexture->GetImageView();
        imageInfo.sampler = vulkanTexture->GetSampler();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = textureSet;
        descriptorWrite.dstBinding = slot; // 对应 Shader 里的 binding = slot
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

        // 立即绑定该 Set 到管线的 Set 1 槽位
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                vulkanPipeline->GetVulkanPipelineLayout(), 
                                1, 1, &textureSet, 0, nullptr);
    }

    void VulkanRenderCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Framebuffer>& framebuffer, uint32_t attachmentIndex, bool isDepth) {
        auto vulkanFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(framebuffer);
        if (!vulkanFBO) return;
        VkImageView view = isDepth ? vulkanFBO->GetDepthAttachmentImageView() : vulkanFBO->GetColorAttachmentImageView(attachmentIndex);
        if (view == VK_NULL_HANDLE) return;
        m_PendingImageInfos[slot] = { vulkanFBO->GetSampler(), view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    }

    void VulkanRenderCommandBuffer::BindTextureCube(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<TextureCube>& texture) {
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        auto vulkanTexture = std::dynamic_pointer_cast<VulkanTextureCube>(texture);
        
        if (!vulkanPipeline || !vulkanTexture) return;

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();

        VkDescriptorSet textureSet = vulkanPipeline->GetNextTextureDescriptorSet();

        // AYAYA_CORE_WARN("Debug TextureCube -> View: {0}, Sampler: {1}", 
        //        (void*)vulkanTexture->GetImageView(), 
        //        (void*)vulkanTexture->GetSampler());
        if (vulkanTexture->GetImageView() == VK_NULL_HANDLE || vulkanTexture->GetSampler() == VK_NULL_HANDLE) {
            AYAYA_CORE_ERROR("Vulkan: Attempted to bind an incomplete TextureCube! (View or Sampler is NULL)");
            return;
        }
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = vulkanTexture->GetImageView(); // 这里拿到的是 CUBE 类型的 View
        imageInfo.sampler = vulkanTexture->GetSampler();     // 必须提供 Sampler

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = textureSet;
        descriptorWrite.dstBinding = slot; 
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                vulkanPipeline->GetVulkanPipelineLayout(), 
                                1, 1, &textureSet, 0, nullptr);
    }

    // ==========================================
    // 核心冲刷器：Draw 之前的终极发车！
    // ==========================================
    void VulkanRenderCommandBuffer::FlushDescriptorSets() {
        if (!m_BoundPipeline || m_PendingImageInfos.empty()) return;

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();

        // 从环形缓冲里拿出一个全新的干净集装箱
        VkDescriptorSet newSet = m_BoundPipeline->GetNextTextureDescriptorSet();

        std::vector<VkWriteDescriptorSet> writes;
        // 把小本本里的记录全部写进新集装箱
        for (auto& pair : m_PendingImageInfos) {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = newSet;
            write.dstBinding = pair.first;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &pair.second; // 值存在 map 里，地址稳定
            writes.push_back(write);
        }

        vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
        vkCmdBindDescriptorSets(context->GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_BoundPipeline->GetVulkanPipelineLayout(), 1, 1, &newSet, 0, nullptr);
    }

    // --- 绘制指令 ---
    void VulkanRenderCommandBuffer::DrawIndexed(const std::shared_ptr<Mesh>& mesh, uint32_t indexCount) {
        if (!m_BoundPipeline) return;
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        uint32_t currentFrame = context->GetCurrentFrameIndex() % 3;
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();

        // 1. 【核心修复】：绑定相机数据 (Set 0)
        VkDescriptorSet cameraSet = m_BoundPipeline->GetVulkanDescriptorSet(0);
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
        FlushDescriptorSets(); // 发车！
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        vkCmdDraw(context->GetCurrentCommandBuffer(), vertexCount, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawArrays(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount) {
        FlushDescriptorSets(); // 发车！
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        uint32_t count = vertexCount ? vertexCount : mesh->GetVertexCount();
        vkCmdDraw(context->GetCurrentCommandBuffer(), count, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawTriangleStrip(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount) {
        FlushDescriptorSets(); // 发车！
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        uint32_t count = vertexCount ? vertexCount : mesh->GetVertexCount();
        vkCmdDraw(context->GetCurrentCommandBuffer(), count, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawTriangleStrip(uint32_t vertexCount) {
        FlushDescriptorSets(); // 发车！
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        vkCmdDraw(context->GetCurrentCommandBuffer(), vertexCount, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawArrays(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) {
        if (!m_BoundPipeline) return;

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();

        // ==========================================
        // 1. 【核心修复】：必须绑定 Set 0！
        // 哪怕后处理 Shader 里没用到，Vulkan 也要求布局槽位必须对齐填满！
        // ==========================================
        VkDescriptorSet cameraSet = m_BoundPipeline->GetVulkanDescriptorSet(0);
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
        memoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            cmdBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            1, &memoryBarrier,
            0, nullptr,
            0, nullptr
        );
    }

}