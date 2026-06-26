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

        uint32_t width  = vulkanFBO->GetSpecification().Width;
        uint32_t height = vulkanFBO->GetSpecification().Height;
        VkAttachmentLoadOp loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;

        // 构建颜色附件
        uint32_t colorCount = vulkanFBO->GetColorAttachmentCount();
        std::vector<VkRenderingAttachmentInfo> colorAttachments(colorCount);
        for (uint32_t i = 0; i < colorCount; i++) {
            VkRenderingAttachmentInfo& att = colorAttachments[i];
            att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            att.imageView = vulkanFBO->GetColorAttachmentImageView(i);
            att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            att.loadOp = loadOp;
            att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            att.clearValue.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
        }

        // Per-attachment clear color overrides (e.g. WBOIT attachment 1 needs 1.0)
        if (!m_PendingClearColors.empty()) {
            for (uint32_t i = 0; i < std::min(colorCount, (uint32_t)m_PendingClearColors.size()); i++) {
                auto& c = m_PendingClearColors[i];
                colorAttachments[i].clearValue.color = {{c.r, c.g, c.b, c.a}};
            }
            m_PendingClearColors.clear();
        }

        // 构建深度附件
        VkRenderingAttachmentInfo depthAttachment{};
        if (vulkanFBO->HasDepthAttachment()) {
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = vulkanFBO->GetDepthAttachmentImageView();
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = loadOp;  // 跟随 color: CLEAR或LOAD
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil = { 1.0f, 0 };
        }

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = { {0, 0}, {width, height} };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = colorCount;
        renderingInfo.pColorAttachments = colorAttachments.data();
        renderingInfo.pDepthAttachment = vulkanFBO->HasDepthAttachment() ? &depthAttachment : nullptr;

        vkCmdBeginRendering(cmdBuffer, &renderingInfo);

        // Vulkan 负高度视口 (Y轴翻转，兼容 OpenGL 投影矩阵)
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = (float)height;
        viewport.width = (float)width;
        viewport.height = -(float)height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = { width, height };
        vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
    }

    void VulkanRenderCommandBuffer::EndRenderPass() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        vkCmdEndRendering(context->GetCurrentCommandBuffer());
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

            // Bindless 纹理管线：绑定全局纹理数组
            if (vulkanPipeline->GetSpecification().UseBindlessTextures) {
                VkDescriptorSet bindlessSet = context->GetBindlessSet();
                uint32_t texSet = vulkanPipeline->GetTextureSetIndex();
                vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    vulkanPipeline->GetVulkanPipelineLayout(), texSet, 1, &bindlessSet, 0, nullptr);
            }
            // 非 bindless 管线：绑定有效的 Set 1 覆盖可能残留的 bindless set。
            // bindless set 的 layout 带有 UPDATE_AFTER_BIND_POOL_BIT，与本管线的 Set 1 layout 不兼容。
            else if (vulkanPipeline->GetDescriptorSetLayoutCount() > 1) {
                VkDescriptorSet texSet = vulkanPipeline->GetNextTextureDescriptorSet();
                uint32_t texSlot = vulkanPipeline->GetTextureSetIndex();
                if (texSet != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        vulkanPipeline->GetVulkanPipelineLayout(), texSlot, 1, &texSet, 0, nullptr);
                }
            }

            // 【核心】：切换管线时，清空之前记的小本本，并记住新管线
            if (m_BoundPipeline.get() != vulkanPipeline.get()) {
                m_PendingImageInfos.clear();
            }
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
        // Prefer shadow sampler (hardware PCF) for depth FBOs that have one
        VkSampler sampler = vulkanFBO->GetShadowSampler();
        if (sampler == VK_NULL_HANDLE) sampler = vulkanFBO->GetSampler();
        m_PendingImageInfos[slot] = { sampler, view, layout };
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
        // Bindless 管线不需要 per-draw descriptor set 更新
        if (!m_BoundPipeline || m_BoundPipeline->GetSpecification().UseBindlessTextures) return;

        // 环形缓冲每帧返回新 Set，必须写入，不能因为有缓存就跳过
        if (m_PendingImageInfos.empty()) return;

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();

        VkDescriptorSet newSet = m_BoundPipeline->GetNextTextureDescriptorSet();
        if (newSet == VK_NULL_HANDLE) {
            AYAYA_CORE_ERROR("FlushDescriptorSets: newSet is VK_NULL_HANDLE, skipping draw");
            m_DescriptorSetDirty = false;
            return;
        }

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
        uint32_t texSet = (m_BoundPipeline->GetDescriptorSetLayoutCount() == 1) ? 0 : 1;
        vkCmdBindDescriptorSets(context->GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_BoundPipeline->GetVulkanPipelineLayout(), texSet, 1, &newSet, 0, nullptr);

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
        m_DrawCallCount++;
        m_TriangleCount += count / 3;
    }

    void VulkanRenderCommandBuffer::DrawIndexedInstanced(
            const std::shared_ptr<Mesh>& mesh,
            uint32_t indexCount,
            uint32_t instanceCount,
            uint32_t firstInstance) {
        if (!m_BoundPipeline || !mesh || instanceCount == 0) return;
        auto context = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        uint32_t currentFrame = context->GetCurrentFrameIndex() % 3;
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();

        // Bind camera UBO (Set 0) for current frame
        VkDescriptorSet cameraSet = m_BoundPipeline->GetVulkanDescriptorSet(0, currentFrame);
        if (cameraSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_BoundPipeline->GetVulkanPipelineLayout(), 0, 1, &cameraSet, 0, nullptr);
        }

        // Bind material textures (Set 1)
        FlushDescriptorSets();

        // Bind VBO + IBO
        auto vb = std::dynamic_pointer_cast<VulkanVertexBuffer>(mesh->GetVertexBuffer());
        auto ib = std::dynamic_pointer_cast<VulkanIndexBuffer>(mesh->GetIndexBuffer());
        if (vb && ib) {
            VkBuffer vbs[] = { vb->GetVulkanBuffer() };
            VkDeviceSize off[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);
            vkCmdBindIndexBuffer(cmd, ib->GetVulkanBuffer(), 0, VK_INDEX_TYPE_UINT32);
        }

        uint32_t count = indexCount ? indexCount : mesh->GetIndexCount();
        vkCmdDrawIndexed(cmd, count, instanceCount, 0, 0, firstInstance);
        m_DrawCallCount++;
        m_TriangleCount += (count / 3) * instanceCount;
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
        m_DrawCallCount++;
        m_TriangleCount += vertexCount / 3;
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
        m_DrawCallCount++;
        m_TriangleCount += count / 3;
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
        m_DrawCallCount++;
        m_TriangleCount += count / 3;
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
        m_DrawCallCount++;
        m_TriangleCount += vertexCount / 3;
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
        m_DrawCallCount++;
        m_TriangleCount += count / 3;
    }

    void VulkanRenderCommandBuffer::WriteTimestamp(uint32_t queryIndex, bool topOfPipe) {
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!ctx || ctx->GetTimestampPool() == VK_NULL_HANDLE) return;
        VkPipelineStageFlagBits stage = topOfPipe
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        vkCmdWriteTimestamp(ctx->GetCurrentCommandBuffer(), stage,
            ctx->GetTimestampPool(), queryIndex);
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

    // ==========================================
    // RHI 隔离层：引擎通用 ImageLayout → Vulkan VkImageLayout
    // ==========================================
    static VkImageLayout ToVkImageLayout(ImageLayout layout) {
        switch (layout) {
            case ImageLayout::Undefined:                      return VK_IMAGE_LAYOUT_UNDEFINED;
            case ImageLayout::ColorAttachmentOptimal:         return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            case ImageLayout::DepthStencilAttachmentOptimal:  return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            case ImageLayout::ShaderReadOnlyOptimal:          return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case ImageLayout::DepthStencilReadOnlyOptimal:    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            case ImageLayout::TransferSrcOptimal:             return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            case ImageLayout::TransferDstOptimal:             return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            case ImageLayout::PresentSrc:                     return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            default: AYAYA_CORE_ASSERT(false, "Unknown ImageLayout!"); return VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    // ==========================================
    // 精准 Image Layout 转换屏障 (替代全局 Barrier)
    // ==========================================
    void VulkanRenderCommandBuffer::TransitionImageLayout(const std::shared_ptr<Framebuffer>& fbo,
                                                         uint32_t attachmentIndex,
                                                         ImageLayout oldLayout,
                                                         ImageLayout newLayout) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmdBuffer = context->GetCurrentCommandBuffer();

        auto vulkanFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(fbo);
        if (!vulkanFBO) return;

        VkImage image = vulkanFBO->GetColorAttachmentImage(attachmentIndex);
        // depth-only FBO (e.g. ShadowMap) — barrier the depth attachment instead
        bool isDepth = false;
        if (image == VK_NULL_HANDLE) {
            image = vulkanFBO->GetDepthAttachmentImage();
            isDepth = true;
        }
        if (image == VK_NULL_HANDLE) return;

        VkImageLayout vkOld = ToVkImageLayout(oldLayout);
        VkImageLayout vkNew = ToVkImageLayout(newLayout);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = vkOld;
        barrier.newLayout = vkNew;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;

        if (isDepth ||
            vkNew == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
            vkNew == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        } else {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        VkPipelineStageFlags sourceStage, destinationStage;

        if (vkOld == VK_IMAGE_LAYOUT_UNDEFINED &&
            vkNew == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (vkOld == VK_IMAGE_LAYOUT_UNDEFINED &&
                 vkNew == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (vkOld == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                 vkNew == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (vkOld == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                 vkNew == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            sourceStage      = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (vkOld == VK_IMAGE_LAYOUT_UNDEFINED &&
                 vkNew == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (vkOld == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
                 vkNew == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            // 纯深度 FBO：下帧写入前 → depth read-only → depth attachment
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            sourceStage      = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (vkOld == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
                 vkNew == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
            // 纯深度 FBO：写入后转可读 → depth attachment → depth read-only
            barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage      = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else {
            AYAYA_CORE_ERROR("TransitionImageLayout: Unsupported transition {0} -> {1}",
                (int)oldLayout, (int)newLayout);
            return;
        }

        vkCmdPipelineBarrier(cmdBuffer, sourceStage, destinationStage,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

}