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

            // ==========================================
            // 【核心修缮】：同时将 UBO(0) 和 Texture(1) 的集装箱绑定到管线上！
            // 不绑定的话，着色器将无法读取任何数据。
            // ==========================================
            VkDescriptorSet sets[] = {
                vulkanPipeline->GetVulkanDescriptorSet(0),
                vulkanPipeline->GetVulkanDescriptorSet(1)
            };

            if (sets[0] != VK_NULL_HANDLE && sets[1] != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipelineLayout(), 0, 2, sets, 0, nullptr);
            }
        } else {
            AYAYA_CORE_ERROR("Vulkan Error: Attempted to bind a NULL Pipeline!");
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

    void VulkanRenderCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Texture2D>& texture) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        auto vulkanTex = std::dynamic_pointer_cast<VulkanTexture2D>(texture);

        if (!vulkanPipeline || !vulkanTex) return;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = vulkanTex->GetImageView();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.sampler = vulkanTex->GetSampler();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        
        // 【核心修改】：贴图都在 Set 1 中，必须传入 1 ！！！
        descriptorWrite.dstSet = vulkanPipeline->GetVulkanDescriptorSet(1); 
        descriptorWrite.dstBinding = slot;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkDeviceWaitIdle(context->GetDevice()); // TODO: 临时暴力锁，后期需改为动态 DescriptorPool
        vkUpdateDescriptorSets(context->GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }

    void VulkanRenderCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Framebuffer>& framebuffer, uint32_t attachmentIndex, bool isDepth) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        auto vulkanFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(framebuffer);

        if (!vulkanPipeline || !vulkanFBO) return;

        VkDescriptorImageInfo imageInfo{};
        
        // ==========================================
        // 【核心修复 2】：解除注释，真正绑定 FBO 贴图！
        // ==========================================
        if (isDepth) {
            imageInfo.imageView = vulkanFBO->GetDepthAttachmentImageView(); // 修改这里
        } else {
            imageInfo.imageView = vulkanFBO->GetColorAttachmentImageView(attachmentIndex); // 修改这里
        }
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.sampler = vulkanFBO->GetSampler(); // 修改这里

        if (imageInfo.imageView == VK_NULL_HANDLE) {
            AYAYA_CORE_WARN("Failed to bind Framebuffer Texture: ImageView is NULL!");
            return;
        }

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = vulkanPipeline->GetVulkanDescriptorSet(1); // Set 1
        descriptorWrite.dstBinding = slot;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkDeviceWaitIdle(context->GetDevice());
        vkUpdateDescriptorSets(context->GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }

    void VulkanRenderCommandBuffer::BindTextureCube(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<TextureCube>& textureCube) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        auto vulkanTex = std::dynamic_pointer_cast<VulkanTextureCube>(textureCube);

        if (!vulkanPipeline || !vulkanTex) return;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = vulkanTex->GetImageView();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.sampler = vulkanTex->GetSampler(); 

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = vulkanPipeline->GetVulkanDescriptorSet(1); // 【核心修改】：写入 Set 1
        descriptorWrite.dstBinding = slot;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkDeviceWaitIdle(context->GetDevice());
        vkUpdateDescriptorSets(context->GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }

    // --- 绘制指令 ---
    void VulkanRenderCommandBuffer::DrawIndexed(const std::shared_ptr<Mesh>& mesh, uint32_t indexCount) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (!mesh) return;

        VkCommandBuffer cmdBuffer = context->GetCurrentCommandBuffer();

        // ==========================================
        // 1. 绑定 VBO
        // ==========================================
        auto vbo = std::dynamic_pointer_cast<VulkanVertexBuffer>(mesh->GetVertexBuffer());
        if (vbo && vbo->GetVulkanBuffer() != VK_NULL_HANDLE) {
            VkBuffer vertexBuffers[] = { vbo->GetVulkanBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);
        }

        // ==========================================
        // 2. 绑定 IBO 并拦截致命错误
        // ==========================================
        auto ibo = std::dynamic_pointer_cast<VulkanIndexBuffer>(mesh->GetIndexBuffer());
        if (ibo && ibo->GetVulkanBuffer() != VK_NULL_HANDLE) {
            // 你的引擎通常使用 uint32_t 的索引
            vkCmdBindIndexBuffer(cmdBuffer, ibo->GetVulkanBuffer(), 0, VK_INDEX_TYPE_UINT32);
        } else {
            // 【防崩魔法】：如果没有索引缓冲区，强行 Draw 会让驱动爆炸，我们直接跳过绘制！
            AYAYA_CORE_ERROR("Vulkan Error: Trying to DrawIndexed but no valid Index Buffer found!");
            return; 
        }

        uint32_t count = indexCount ? indexCount : mesh->GetIndexCount();
        vkCmdDrawIndexed(cmdBuffer, count, 1, 0, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawArrays(uint32_t vertexCount) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        vkCmdDraw(context->GetCurrentCommandBuffer(), vertexCount, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawArrays(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        uint32_t count = vertexCount ? vertexCount : mesh->GetVertexCount();
        vkCmdDraw(context->GetCurrentCommandBuffer(), count, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawTriangleStrip(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        uint32_t count = vertexCount ? vertexCount : mesh->GetVertexCount();
        vkCmdDraw(context->GetCurrentCommandBuffer(), count, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawTriangleStrip(uint32_t vertexCount) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        vkCmdDraw(context->GetCurrentCommandBuffer(), vertexCount, 1, 0, 0);
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