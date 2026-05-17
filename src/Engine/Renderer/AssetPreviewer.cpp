#include "ayapch.h"
#include "AssetPreviewer.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Pipeline.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/UniformBuffer.hpp"
#include "Renderer/Model.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Asset/AssetManager.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Vulkan-specific
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Platform/Vulkan/VulkanTexture2D.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanBuffer.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"

namespace Ayaya {

    static constexpr uint32_t kPreviewSize = 256;

    // ---- Shared statics ----
    std::shared_ptr<Shader>      AssetPreviewer::s_PreviewShader;
    std::shared_ptr<Framebuffer> AssetPreviewer::s_PreviewFBO;
    std::shared_ptr<Texture2D>   AssetPreviewer::s_RealtimeWrapper;
    std::shared_ptr<Pipeline>      AssetPreviewer::s_PreviewPipeline;
    std::shared_ptr<UniformBuffer> AssetPreviewer::s_CameraUBO;
    UUID     AssetPreviewer::s_LastModelHandle = 0;
    uint32_t AssetPreviewer::s_FboSize = kPreviewSize;
    glm::mat4 AssetPreviewer::s_ViewMatrix = glm::mat4(1.0f);
    glm::mat4 AssetPreviewer::s_ProjMatrix = glm::mat4(1.0f);
    glm::vec3 AssetPreviewer::s_CameraPos = glm::vec3(0.0f, 0.0f, 5.0f);

    // ---- Push constant layout (must match shader, max 128 bytes) ----
    struct PreviewPushConstants {
        glm::mat4 ModelMatrix;  // offset 0,  size 64
        glm::vec4 Albedo;        // offset 64, size 16
        glm::vec4 LightDir;      // offset 80, size 16
        glm::vec4 LightColor;    // offset 96, size 16
        glm::vec4 Ambient;       // offset 112, size 16
    };
    static_assert(sizeof(PreviewPushConstants) == 128, "Push constant size mismatch");

    // ---- Camera UBO layout (matches set=0 binding=0 in shader) ----
    struct PreviewCameraUBO {
        glm::mat4 ViewProjection;
        glm::vec3 CameraPosition;
        float _pad;
    };

    // ---- AABB helpers ----

    static glm::vec3 MergeAABBMin(const std::shared_ptr<Model>& model) {
        glm::vec3 minVal(FLT_MAX);
        for (auto& mesh : model->GetMeshes())
            minVal = glm::min(minVal, mesh->GetAABB().Min);
        return minVal;
    }

    static glm::vec3 MergeAABBMax(const std::shared_ptr<Model>& model) {
        glm::vec3 maxVal(-FLT_MAX);
        for (auto& mesh : model->GetMeshes())
            maxVal = glm::max(maxVal, mesh->GetAABB().Max);
        return maxVal;
    }

    static glm::vec3 ComputeModelCenter(const std::shared_ptr<Model>& model) {
        glm::vec3 minVal = MergeAABBMin(model);
        glm::vec3 maxVal = MergeAABBMax(model);
        if (minVal.x == FLT_MAX) return glm::vec3(0.0f);
        return (minVal + maxVal) * 0.5f;
    }

    static float ComputeModelBoundingRadius(const std::shared_ptr<Model>& model) {
        glm::vec3 center = ComputeModelCenter(model);
        float maxRadius = 0.001f;
        for (auto& mesh : model->GetMeshes()) {
            const AABB& box = mesh->GetAABB();
            glm::vec3 corners[8] = {
                glm::vec3(box.Min.x, box.Min.y, box.Min.z), glm::vec3(box.Max.x, box.Min.y, box.Min.z),
                glm::vec3(box.Min.x, box.Max.y, box.Min.z), glm::vec3(box.Max.x, box.Max.y, box.Min.z),
                glm::vec3(box.Min.x, box.Min.y, box.Max.z), glm::vec3(box.Max.x, box.Min.y, box.Max.z),
                glm::vec3(box.Min.x, box.Max.y, box.Max.z), glm::vec3(box.Max.x, box.Max.y, box.Max.z),
            };
            for (auto& c : corners) { float d = glm::distance(center, c); if (d > maxRadius) maxRadius = d; }
        }
        return maxRadius;
    }

    // ---- Camera auto-framing ----

    void AssetPreviewer::AutoFrameCamera(const std::shared_ptr<Model>& model, glm::vec2 orbitAngle, float fovY) {
        float radius = ComputeModelBoundingRadius(model);
        float distance = radius / glm::sin(fovY * 0.5f);
        if (distance < radius * 1.2f) distance = radius * 1.5f;

        float pitch = glm::clamp(orbitAngle.x, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
        float yaw   = orbitAngle.y;

        s_CameraPos.x = distance * glm::cos(pitch) * glm::sin(yaw);
        s_CameraPos.y = distance * glm::sin(pitch);
        s_CameraPos.z = distance * glm::cos(pitch) * glm::cos(yaw);

        s_ViewMatrix = glm::lookAt(s_CameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        float nearClip = glm::max(distance * 0.001f, 0.01f);
        float farClip  = glm::max(distance * 10.0f, 100.0f);
        s_ProjMatrix = glm::perspective(fovY, 1.0f, nearClip, farClip);
    }

    // ==== OpenGL render path ====

    void AssetPreviewer::RenderModel_OpenGL(const std::shared_ptr<Model>& model) {
        GLuint fboID = (GLuint)(uintptr_t)AssetPreviewer::s_PreviewFBO->GetRendererID();

        GLint prevFBO = 0, prevVP[4];
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
        glGetIntegerv(GL_VIEWPORT, prevVP);
        GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
        GLboolean prevCull  = glIsEnabled(GL_CULL_FACE);

        glBindFramebuffer(GL_FRAMEBUFFER, fboID);
        glViewport(0, 0, kPreviewSize, kPreviewSize);
        glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        AssetPreviewer::s_PreviewShader->Bind();

        glm::mat4 viewProj = AssetPreviewer::s_ProjMatrix * AssetPreviewer::s_ViewMatrix;
        AssetPreviewer::s_PreviewShader->SetMat4("u_ViewProjection", viewProj);
        AssetPreviewer::s_PreviewShader->SetFloat3("u_CameraPos", AssetPreviewer::s_CameraPos);

        glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.8f));
        AssetPreviewer::s_PreviewShader->SetFloat3("u_LightDir", lightDir);
        AssetPreviewer::s_PreviewShader->SetFloat3("u_LightColor", glm::vec3(1.0f, 0.98f, 0.95f));
        AssetPreviewer::s_PreviewShader->SetFloat3("u_Ambient", glm::vec3(0.15f, 0.15f, 0.17f));
        AssetPreviewer::s_PreviewShader->SetFloat3("u_Albedo", glm::vec3(0.8f, 0.8f, 0.8f));

        glm::vec3 center = ComputeModelCenter(model);
        glm::mat4 centering = glm::translate(glm::mat4(1.0f), -center);
        glm::mat3 normalMat3 = glm::transpose(glm::inverse(glm::mat3(centering)));

        for (auto& mesh : model->GetMeshes()) {
            AssetPreviewer::s_PreviewShader->SetMat4("u_Transform", centering);
            AssetPreviewer::s_PreviewShader->SetMat4("u_NormalMatrix", glm::mat4(normalMat3));
            auto va = mesh->GetVertexArray();
            if (!va || mesh->GetIndexCount() == 0) continue;
            va->Bind();
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        }

        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
        if (!prevDepth) glDisable(GL_DEPTH_TEST);
        if (!prevCull)  glDisable(GL_CULL_FACE);
    }

    // ==== Vulkan render path ====

    void AssetPreviewer::RenderModel_Vulkan(const std::shared_ptr<Model>& model) {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;

        // Update camera UBO
        PreviewCameraUBO camData;
        camData.ViewProjection = AssetPreviewer::s_ProjMatrix * AssetPreviewer::s_ViewMatrix;
        camData.CameraPosition = AssetPreviewer::s_CameraPos;
        AssetPreviewer::s_CameraUBO->SetData(&camData, sizeof(PreviewCameraUBO));

        // Build push constants
        PreviewPushConstants push;
        glm::vec3 center = ComputeModelCenter(model);
        push.ModelMatrix = glm::translate(glm::mat4(1.0f), -center);
        push.Albedo      = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        push.LightDir    = glm::vec4(glm::normalize(glm::vec3(0.5f, 1.0f, 0.8f)), 0.0f);
        push.LightColor  = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);
        push.Ambient     = glm::vec4(0.15f, 0.15f, 0.17f, 1.0f);

        auto cmd = RenderCommandBuffer::Create();
        if (!cmd) return;

        cmd->Begin();
        cmd->BeginRenderPass(AssetPreviewer::s_PreviewFBO, true,
                             glm::vec4(0.05f, 0.05f, 0.06f, 1.0f));
        cmd->BindPipeline(AssetPreviewer::s_PreviewPipeline);

        for (auto& mesh : model->GetMeshes()) {
            if (mesh->GetIndexCount() == 0) continue;
            cmd->PushConstantData(AssetPreviewer::s_PreviewPipeline, &push, sizeof(push));
            cmd->DrawIndexed(mesh, mesh->GetIndexCount());
        }

        cmd->EndRenderPass();
        cmd->End();
    }

    // ---- Unified RenderModel dispatcher ----

    void AssetPreviewer::RenderModel(const std::shared_ptr<Model>& model) {
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
            RenderModel_Vulkan(model);
        else
            RenderModel_OpenGL(model);
    }

    // ---- Readback: realtime (wraps FBO attachment, cached) ----

    std::shared_ptr<Texture2D> AssetPreviewer::ReadbackRealtime() {
        if (!s_RealtimeWrapper) {
            if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                auto vkFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO);
                if (vkFBO)
                    s_RealtimeWrapper = std::make_shared<VulkanTexture2D>(
                        (void*)vkFBO->GetColorAttachmentImageView(0),
                        kPreviewSize, kPreviewSize);
            } else {
                void* id = s_PreviewFBO->GetColorAttachmentRendererID(0);
                s_RealtimeWrapper = Texture2D::Create(id, kPreviewSize, kPreviewSize);
            }
            AYAYA_CORE_INFO("AssetPreviewer: Realtime wrapper created, texID={}",
                           (uint64_t)(uintptr_t)s_PreviewFBO->GetColorAttachmentRendererID(0));
        }
        return s_RealtimeWrapper;
    }

    // ---- Readback: standalone deep copy (for thumbnail caching) ----

    std::shared_ptr<Texture2D> AssetPreviewer::ReadbackStandalone() {
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            // GPU→CPU readback via staging buffer
            auto vkFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO);
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            if (!vkFBO || !vkCtx) return nullptr;

            VkDevice device = vkCtx->GetDevice();
            VmaAllocator allocator = vkCtx->GetAllocator();
            uint32_t dataSize = kPreviewSize * kPreviewSize * 4;

            // Create staging buffer
            VkBufferCreateInfo bufInfo{};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = dataSize;
            bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            VkBuffer stagingBuf;
            VmaAllocation stagingAlloc;
            vmaCreateBuffer(allocator, &bufInfo, &allocInfo, &stagingBuf, &stagingAlloc, nullptr);

            VkCommandBuffer cmd = vkCtx->BeginSingleTimeCommands();

            // Transition FBO image to TRANSFER_SRC
            {
                VkImageMemoryBarrier b{};
                b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image = vkFBO->GetColorAttachmentImage(0);
                b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                b.subresourceRange.levelCount = 1;
                b.subresourceRange.layerCount = 1;
                b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &b);
            }

            // Copy image to buffer
            {
                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = {0, 0, 0};
                region.imageExtent = {kPreviewSize, kPreviewSize, 1};
                vkCmdCopyImageToBuffer(cmd,
                    vkFBO->GetColorAttachmentImage(0), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    stagingBuf, 1, &region);
            }

            // Transition back to SHADER_READ_ONLY
            {
                VkImageMemoryBarrier b{};
                b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image = vkFBO->GetColorAttachmentImage(0);
                b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                b.subresourceRange.levelCount = 1;
                b.subresourceRange.layerCount = 1;
                b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &b);
            }

            vkCtx->EndSingleTimeCommands(cmd);

            // Map and copy pixels
            void* mapped = nullptr;
            vmaMapMemory(allocator, stagingAlloc, &mapped);
            auto tex = Texture2D::Create(kPreviewSize, kPreviewSize);
            if (tex && mapped)
                tex->SetData(mapped, dataSize);
            vmaUnmapMemory(allocator, stagingAlloc);
            vmaDestroyBuffer(allocator, stagingBuf, stagingAlloc);

            return tex;
        }

        // OpenGL: simple glReadPixels
        GLuint fboID = (GLuint)(uintptr_t)s_PreviewFBO->GetRendererID();
        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, fboID);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        while (glGetError() != GL_NO_ERROR) {}
        std::vector<uint8_t> pixels(kPreviewSize * kPreviewSize * 4);
        glReadPixels(0, 0, kPreviewSize, kPreviewSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        GLenum err = glGetError();
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);

        if (err != GL_NO_ERROR) {
            AYAYA_CORE_ERROR("AssetPreviewer: glReadPixels error 0x{0:x}", err);
            return nullptr;
        }
        auto tex = Texture2D::Create(kPreviewSize, kPreviewSize);
        if (tex) tex->SetData(pixels.data(), (uint32_t)pixels.size());
        return tex;
    }

    // ---- Lifecycle ----

    void AssetPreviewer::Init() {
        s_PreviewShader = Shader::Create("Preview/preview.vert", "Preview/preview.frag");
        AYAYA_CORE_INFO("AssetPreviewer: Shader loaded, name='{}'", s_PreviewShader ? s_PreviewShader->GetName() : "NULL");

        FramebufferSpecification spec;
        spec.Samples = 1;
        spec.Width  = kPreviewSize;
        spec.Height = kPreviewSize;
        spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        s_PreviewFBO = Framebuffer::Create(spec);

        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            // Camera UBO for Vulkan (matches set=0 binding=0 in shader)
            s_CameraUBO = UniformBuffer::Create(sizeof(PreviewCameraUBO), 0);

            // Pipeline for Vulkan preview rendering
            PipelineSpecification pipeSpec;
            pipeSpec.Shader = s_PreviewShader;
            pipeSpec.TargetFramebuffer = s_PreviewFBO;
            // Vertex layout must match mesh's 4-attribute format (44 bytes/vertex)
            // even though the shader only uses position + normal.
            pipeSpec.Layout = {
                { ShaderDataType::Float3, "a_Position" },
                { ShaderDataType::Float3, "a_Normal"   },
                { ShaderDataType::Float2, "a_TexCoord" },
                { ShaderDataType::Float3, "a_Tangent"  },
            };
            pipeSpec.DepthTest = true;
            pipeSpec.DepthWrite = true;
            pipeSpec.Blend = false;
            pipeSpec.BackfaceCulling = CullMode::Back;
            pipeSpec.NoTextureDescriptors = true; // preview shader has no textures
            s_PreviewPipeline = Pipeline::Create(pipeSpec);
        }

        AYAYA_CORE_INFO("AssetPreviewer: Ready ({0}), {1}x{1}",
                       RendererAPI::GetAPI() == RendererAPI::API::Vulkan ? "Vulkan" : "OpenGL",
                       kPreviewSize);
    }

    void AssetPreviewer::Shutdown() {
        s_RealtimeWrapper.reset();
        s_PreviewPipeline.reset();
        s_CameraUBO.reset();
        s_PreviewFBO.reset();
        s_PreviewShader.reset();
        s_LastModelHandle = 0;
        AYAYA_CORE_INFO("AssetPreviewer: Shutdown.");
    }

    // ---- Public API ----

    std::shared_ptr<Texture2D> AssetPreviewer::GenerateThumbnail(UUID modelHandle, uint32_t size) {
        (void)size;
        auto model = AssetManager::GetAsset<Model>(modelHandle);
        if (!model) {
            AssetManager::RequestAsyncLoad(modelHandle);
            return nullptr;
        }
        if (model->GetMeshes().empty()) return nullptr;

        AutoFrameCamera(model, glm::vec2(0.3f, -0.6f), glm::radians(45.0f));

        // === Vulkan: synchronous render + readback in one command buffer ===
        // Cannot use RenderModel (frame cmd buffer) + ReadbackStandalone (temp cmd buffer)
        // because the frame buffer hasn't been submitted yet → FBO is empty → magenta.
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            auto vkFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO);
            auto vkPipeline = std::dynamic_pointer_cast<VulkanPipeline>(s_PreviewPipeline);
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            if (!vkFBO || !vkPipeline || !vkCtx) return nullptr;

            // Update camera UBO
            PreviewCameraUBO camData;
            camData.ViewProjection = s_ProjMatrix * s_ViewMatrix;
            camData.CameraPosition = s_CameraPos;
            s_CameraUBO->SetData(&camData, sizeof(PreviewCameraUBO));

            // Push constants
            PreviewPushConstants push;
            glm::vec3 center = ComputeModelCenter(model);
            push.ModelMatrix = glm::translate(glm::mat4(1.0f), -center);
            push.Albedo      = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
            push.LightDir    = glm::vec4(glm::normalize(glm::vec3(0.5f, 1.0f, 0.8f)), 0.0f);
            push.LightColor  = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);
            push.Ambient     = glm::vec4(0.15f, 0.15f, 0.17f, 1.0f);

            // Create staging buffer for readback
            VmaAllocator allocator = vkCtx->GetAllocator();
            uint32_t dataSize = kPreviewSize * kPreviewSize * 4;
            VkBuffer stagingBuf;
            VmaAllocation stagingAlloc;
            {
                VkBufferCreateInfo bi{};
                bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bi.size = dataSize;
                bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                VmaAllocationCreateInfo ai{};
                ai.usage = VMA_MEMORY_USAGE_AUTO;
                ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
                vmaCreateBuffer(allocator, &bi, &ai, &stagingBuf, &stagingAlloc, nullptr);
            }

            VkCommandBuffer cmd = vkCtx->BeginSingleTimeCommands();

            // === Render pass ===
            {
                VkRenderPassBeginInfo rp{};
                rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp.renderPass = vkFBO->GetVulkanRenderPass();
                rp.framebuffer = vkFBO->GetVulkanFramebuffer();
                rp.renderArea.extent = { kPreviewSize, kPreviewSize };
                VkClearValue cv[2];
                cv[0].color = {{ 0.05f, 0.05f, 0.06f, 1.0f }};
                cv[1].depthStencil = { 1.0f, 0 };
                rp.clearValueCount = 2;
                rp.pClearValues = cv;
                vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport vp{};
                vp.x = 0; vp.y = (float)kPreviewSize;
                vp.width = (float)kPreviewSize; vp.height = -(float)kPreviewSize;
                vp.minDepth = 0; vp.maxDepth = 1;
                vkCmdSetViewport(cmd, 0, 1, &vp);
                VkRect2D scissor{};
                scissor.extent = { kPreviewSize, kPreviewSize };
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  vkPipeline->GetVulkanPipeline());

                uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % 3;
                VkDescriptorSet set0 = vkPipeline->GetVulkanDescriptorSet(0, frameIdx);
                if (set0 != VK_NULL_HANDLE)
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        vkPipeline->GetVulkanPipelineLayout(), 0, 1, &set0, 0, nullptr);

                for (auto& mesh : model->GetMeshes()) {
                    if (mesh->GetIndexCount() == 0) continue;
                    vkCmdPushConstants(cmd, vkPipeline->GetVulkanPipelineLayout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(PreviewPushConstants), &push);

                    auto vkVB = std::dynamic_pointer_cast<VulkanVertexBuffer>(
                        mesh->GetVertexBuffer());
                    auto vkIB = std::dynamic_pointer_cast<VulkanIndexBuffer>(
                        mesh->GetIndexBuffer());
                    if (vkVB && vkIB) {
                        VkBuffer vbs[] = { vkVB->GetVulkanBuffer() };
                        VkDeviceSize off[] = { 0 };
                        vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);
                        vkCmdBindIndexBuffer(cmd, vkIB->GetVulkanBuffer(), 0,
                                             VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(cmd, mesh->GetIndexCount(), 1, 0, 0, 0);
                    }
                }
                vkCmdEndRenderPass(cmd);
            }

            // === Image layout transition + copy to staging ===
            {
                VkImageMemoryBarrier b{};
                b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image = vkFBO->GetColorAttachmentImage(0);
                b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                b.subresourceRange.levelCount = 1;
                b.subresourceRange.layerCount = 1;
                b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &b);
            }
            {
                VkBufferImageCopy region{};
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.layerCount = 1;
                region.imageExtent = { kPreviewSize, kPreviewSize, 1 };
                vkCmdCopyImageToBuffer(cmd,
                    vkFBO->GetColorAttachmentImage(0),
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    stagingBuf, 1, &region);
            }
            {
                VkImageMemoryBarrier b{};
                b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image = vkFBO->GetColorAttachmentImage(0);
                b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                b.subresourceRange.levelCount = 1;
                b.subresourceRange.layerCount = 1;
                b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &b);
            }

            vkCtx->EndSingleTimeCommands(cmd);

            // Map staging buffer and create standalone texture
            void* mapped = nullptr;
            vmaMapMemory(allocator, stagingAlloc, &mapped);
            auto tex = Texture2D::Create(kPreviewSize, kPreviewSize);
            if (tex && mapped)
                tex->SetData(mapped, dataSize);
            vmaUnmapMemory(allocator, stagingAlloc);
            vmaDestroyBuffer(allocator, stagingBuf, stagingAlloc);
            return tex;
        }

        // OpenGL: render + glReadPixels
        RenderModel(model);
        return ReadbackStandalone();
    }

    std::shared_ptr<Texture2D> AssetPreviewer::RenderRealtimePreview(UUID modelHandle, glm::vec2 cameraAngle, uint32_t size) {
        (void)size;
        auto model = AssetManager::GetAsset<Model>(modelHandle);
        if (!model) {
            AssetManager::RequestAsyncLoad(modelHandle);
            return nullptr;
        }
        if (model->GetMeshes().empty()) return nullptr;

        AutoFrameCamera(model, cameraAngle, glm::radians(45.0f));
        RenderModel(model);
        return ReadbackRealtime();
    }

}
