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
#include "Asset/Prefab.hpp"
#include "Renderer/Material.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Components.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_map>

// Vulkan-specific
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Platform/Vulkan/VulkanTexture2D.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanBuffer.hpp"
#include "Platform/Vulkan/VulkanShader.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanUniformBuffer.hpp"

namespace Ayaya {

    static constexpr uint32_t kPreviewSize = 256;

    // ---- Shared statics ----
    std::shared_ptr<Shader>      AssetPreviewer::s_PreviewShader;
    std::shared_ptr<Framebuffer> AssetPreviewer::s_PreviewFBO;
    std::shared_ptr<Framebuffer> AssetPreviewer::s_PreviewFBO_MSAA;
    std::shared_ptr<Texture2D>   AssetPreviewer::s_RealtimeWrapper;
    std::shared_ptr<Pipeline>      AssetPreviewer::s_PreviewPipeline;
    std::shared_ptr<UniformBuffer> AssetPreviewer::s_CameraUBO;
    UUID     AssetPreviewer::s_LastModelHandle = 0;
    uint32_t AssetPreviewer::s_FboSize = kPreviewSize;
    glm::mat4 AssetPreviewer::s_ViewMatrix = glm::mat4(1.0f);
    glm::mat4 AssetPreviewer::s_ProjMatrix = glm::mat4(1.0f);
    glm::vec3 AssetPreviewer::s_CameraPos = glm::vec3(0.0f, 0.0f, 5.0f);

    // ---- Dedicated thumbnail Vulkan resources (isolated from frame loop) ----
    struct ThumbnailVkResources {
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;   // set=0: Camera UBO
        VkBuffer uboBuffer = VK_NULL_HANDLE;
        VmaAllocation uboAllocation = VK_NULL_HANDLE;
        void* mappedData = nullptr;
        bool initialized = false;
    };
    static ThumbnailVkResources s_ThumbnailVk;
    static std::shared_ptr<Model> s_SphereModel;  // cached sphere for material thumbnails

    // ---- PBR pipeline (manual Vulkan, zero main-pipeline interaction) ----
    struct ThumbnailPbrVk {
        VkDescriptorSetLayout set0Layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout set1Layout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet set0 = VK_NULL_HANDLE;
        VkDescriptorSet set1[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkBuffer uboBuffer = VK_NULL_HANDLE;
        VmaAllocation uboAlloc = VK_NULL_HANDLE;
        void* mappedData = nullptr;
        bool initialized = false;
    };
    static ThumbnailPbrVk s_PbrVk;
    static std::shared_ptr<Texture2D> s_PbrBlackTex;

    // ---- Queue for pre-frame batch processing ----
    struct ThumbRequest { UUID handle; int assetType; };
    static std::vector<ThumbRequest> s_ThumbQueue;
    static std::unordered_map<UUID, std::shared_ptr<Texture2D>> s_ThumbCache;

    // ---- PBR push constants (matches preview_pbr shader) ----
    struct PBRPush {
        glm::mat4 ModelMatrix; glm::vec4 Albedo;
        float Metallic, Roughness, AO, Alpha, AlphaCutoff;
        int BlendMode, UseAlbedoMap, UseNormalMap, UseORMMap, UseMetallicMap, UseRoughnessMap, UseAOMap;
    };
    static_assert(sizeof(PBRPush) <= 256, "PBR push too large");

    // ---- Push constant layout (must match shader, max 128 bytes) ----
    struct PreviewPushConstants {
        glm::mat4 ModelMatrix;  // offset 0,  size 64
        glm::vec4 Albedo;        // offset 64, size 16
        glm::vec4 LightDir;      // offset 80, size 16
        glm::vec4 LightColor;    // offset 96, size 16
        glm::vec4 Ambient;       // offset 112, size 16
        int UseAlbedoMap;        // offset 128, size 4
    };
    static_assert(sizeof(PreviewPushConstants) <= 256, "Push constant size exceeds limit");

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
        GLuint fboMSAA  = (GLuint)(uintptr_t)AssetPreviewer::s_PreviewFBO_MSAA->GetRendererID();
        GLuint fboResolve = (GLuint)(uintptr_t)AssetPreviewer::s_PreviewFBO->GetRendererID();

        GLint prevFBO = 0, prevVP[4];
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
        glGetIntegerv(GL_VIEWPORT, prevVP);
        GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);

        // Render to MSAA FBO
        glBindFramebuffer(GL_FRAMEBUFFER, fboMSAA);
        glViewport(0, 0, kPreviewSize, kPreviewSize);
        glClearColor(0.11f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        AssetPreviewer::s_PreviewShader->Bind();

        glm::mat4 viewProj = AssetPreviewer::s_ProjMatrix * AssetPreviewer::s_ViewMatrix;
        AssetPreviewer::s_PreviewShader->SetMat4("u_ViewProjection", viewProj);
        AssetPreviewer::s_PreviewShader->SetFloat3("u_CameraPos", AssetPreviewer::s_CameraPos);
        AssetPreviewer::s_PreviewShader->SetFloat3("u_Albedo", glm::vec3(0.6f, 0.6f, 0.6f));

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

        // MSAA resolve: blit from MSAA FBO to resolve FBO
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fboMSAA);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboResolve);
        glBlitFramebuffer(0, 0, kPreviewSize, kPreviewSize,
                          0, 0, kPreviewSize, kPreviewSize,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
        if (!prevDepth) glDisable(GL_DEPTH_TEST);
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
        push.Albedo      = glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
        push.LightDir    = glm::vec4(glm::normalize(glm::vec3(0.5f, 1.0f, 0.8f)), 0.0f);
        push.LightColor  = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);
        push.Ambient     = glm::vec4(0.15f, 0.15f, 0.17f, 1.0f);

        auto cmd = RenderCommandBuffer::Create();
        if (!cmd) return;

        cmd->Begin();

        // Transition FBO images to attachment layout before BeginRenderPass.
        // The FBO starts in SHADER_READ_ONLY after previous renders.
        {
            auto vkMSAA = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO_MSAA);
            if (vkMSAA) {
                VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
                VkImage colorImg = vkMSAA->GetColorAttachmentImage(0);
                VkImage depthImg = vkMSAA->GetDepthAttachmentImage();

                VkImageMemoryBarrier barriers[2]{};
                uint32_t barrierCount = 0;

                barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barriers[barrierCount].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barriers[barrierCount].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barriers[barrierCount].image = colorImg;
                barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barriers[barrierCount].subresourceRange.levelCount = 1;
                barriers[barrierCount].subresourceRange.layerCount = 1;
                barrierCount++;

                if (depthImg != VK_NULL_HANDLE) {
                    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    barriers[barrierCount].srcAccessMask = 0;
                    barriers[barrierCount].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    barriers[barrierCount].image = depthImg;
                    barriers[barrierCount].subresourceRange.aspectMask =
                        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                    barriers[barrierCount].subresourceRange.levelCount = 1;
                    barriers[barrierCount].subresourceRange.layerCount = 1;
                    barrierCount++;
                }

                vkCmdPipelineBarrier(vkCmd,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    0, 0, nullptr, 0, nullptr, barrierCount, barriers);
            }
        }

        cmd->BeginRenderPass(AssetPreviewer::s_PreviewFBO_MSAA, true,
                             glm::vec4(0.11f, 0.11f, 0.12f, 1.0f));
        cmd->BindPipeline(AssetPreviewer::s_PreviewPipeline);

        for (auto& mesh : model->GetMeshes()) {
            if (mesh->GetIndexCount() == 0) continue;
            cmd->PushConstantData(AssetPreviewer::s_PreviewPipeline, &push, sizeof(push));
            cmd->DrawIndexed(mesh, mesh->GetIndexCount());
        }

        cmd->EndRenderPass();

        // MSAA resolve → non-MSAA FBO for display sampling
        {
            auto vkMSAA = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO_MSAA);
            auto vkResolve = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO);
            if (vkMSAA && vkResolve) {
                VkCommandBuffer cb = vkCtx->GetCurrentCommandBuffer();
                VkImage srcImage = vkMSAA->GetColorAttachmentImage(0);
                VkImage dstImage = vkResolve->GetColorAttachmentImage(0);

                VkImageMemoryBarrier barriers[2]{};
                // MSAA src: EndRenderPass leaves in COLOR_ATTACHMENT_OPTIMAL
                barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barriers[0].image = srcImage;
                barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barriers[0].subresourceRange.levelCount = 1;
                barriers[0].subresourceRange.layerCount = 1;

                // Resolve dst: still in SHADER_READ_ONLY (not yet rendered to in this pass)
                barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barriers[1].image = dstImage;
                barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barriers[1].subresourceRange.levelCount = 1;
                barriers[1].subresourceRange.layerCount = 1;

                // srcStage must cover both barriers: COLOR_ATTACHMENT_WRITE (barrier[0])
                // and SHADER_READ (barrier[1]) from the resolve-dst image.
                vkCmdPipelineBarrier(cb,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 2, barriers);

                VkImageResolve resolveRegion{};
                resolveRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                resolveRegion.srcSubresource.layerCount = 1;
                resolveRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                resolveRegion.dstSubresource.layerCount = 1;
                resolveRegion.extent = { kPreviewSize, kPreviewSize, 1 };
                vkCmdResolveImage(cb, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  1, &resolveRegion);

                VkImageMemoryBarrier postResolve[2]{};
                // MSAA src: back to SHADER_READ_ONLY
                postResolve[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                postResolve[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                postResolve[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                postResolve[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                postResolve[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                postResolve[0].image = srcImage;
                postResolve[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                postResolve[0].subresourceRange.levelCount = 1;
                postResolve[0].subresourceRange.layerCount = 1;

                // Resolve dst: back to SHADER_READ_ONLY
                postResolve[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                postResolve[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                postResolve[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                postResolve[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                postResolve[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                postResolve[1].image = dstImage;
                postResolve[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                postResolve[1].subresourceRange.levelCount = 1;
                postResolve[1].subresourceRange.layerCount = 1;

                vkCmdPipelineBarrier(cb,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 2, postResolve);
            }
        }

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
        spec.Samples = 1;  // resolve target: non-MSAA
        spec.Width  = kPreviewSize;
        spec.Height = kPreviewSize;
        spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        s_PreviewFBO = Framebuffer::Create(spec);

        // MSAA render target
        spec.Samples = 4;
        s_PreviewFBO_MSAA = Framebuffer::Create(spec);

        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            // Camera UBO for Vulkan (matches set=0 binding=0 in shader)
            s_CameraUBO = UniformBuffer::Create(sizeof(PreviewCameraUBO), 0);

            // Pipeline for Vulkan preview rendering
            PipelineSpecification pipeSpec;
            pipeSpec.Shader = s_PreviewShader;
            pipeSpec.TargetFramebuffer = s_PreviewFBO_MSAA; // use MSAA render pass
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
            pipeSpec.BackfaceCulling = CullMode::None; // 双面渲染，避免模型绕序不一致导致漏面
            pipeSpec.NoTextureDescriptors = true; // preview uses solid color only
            s_PreviewPipeline = Pipeline::Create(pipeSpec);

            // Create dedicated thumbnail UBO + descriptor set (isolated from frame loop)
            {
                auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                    Application::Get().GetWindow().GetContext());
                VmaAllocator allocator = vkCtx->GetAllocator();
                VkDevice device = vkCtx->GetDevice();

                VkBufferCreateInfo bufInfo{};
                bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufInfo.size = sizeof(PreviewCameraUBO);
                bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

                VmaAllocationCreateInfo vmaInfo{};
                vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
                vmaInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
                              | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
                vmaInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

                VmaAllocationInfo allocResult{};
                vmaCreateBuffer(allocator, &bufInfo, &vmaInfo,
                                &s_ThumbnailVk.uboBuffer, &s_ThumbnailVk.uboAllocation, &allocResult);
                s_ThumbnailVk.mappedData = allocResult.pMappedData;

                VkDescriptorPoolSize poolSize{};
                poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                poolSize.descriptorCount = 2;

                VkDescriptorPoolCreateInfo poolInfo{};
                poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                poolInfo.poolSizeCount = 1;
                poolInfo.pPoolSizes = &poolSize;
                poolInfo.maxSets = 1;
                vkCreateDescriptorPool(device, &poolInfo, nullptr, &s_ThumbnailVk.pool);

                auto vkPipeline = std::dynamic_pointer_cast<VulkanPipeline>(s_PreviewPipeline);
                VkDescriptorSetLayout layout = vkPipeline->GetDescriptorSetLayout(0);

                VkDescriptorSetAllocateInfo setInfo{};
                setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                setInfo.descriptorPool = s_ThumbnailVk.pool;
                setInfo.descriptorSetCount = 1;
                setInfo.pSetLayouts = &layout;
                vkAllocateDescriptorSets(device, &setInfo, &s_ThumbnailVk.descriptorSet);

                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = s_ThumbnailVk.uboBuffer;
                bufferInfo.offset = 0;
                bufferInfo.range = sizeof(PreviewCameraUBO);

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = s_ThumbnailVk.descriptorSet;
                write.dstBinding = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.descriptorCount = 1;
                write.pBufferInfo = &bufferInfo;
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

                s_ThumbnailVk.initialized = true;
                AYAYA_CORE_INFO("AssetPreviewer: Thumbnail Vulkan resources created");
            }

            // === PBR pipeline (manual Vulkan) ===
            s_PbrBlackTex = Texture2D::Create(1, 1);
            if (s_PbrBlackTex) { uint32_t bv=0; s_PbrBlackTex->SetData(&bv, 4); }
            {
                auto vkCtx2 = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
                auto pbrShader = Shader::Create("Preview/preview_pbr.vert", "Preview/preview_pbr.frag");
                auto vkShader2 = std::dynamic_pointer_cast<VulkanShader>(pbrShader);
                if (vkCtx2 && vkShader2 && s_PreviewFBO_MSAA) {
                    VkDevice dev = vkCtx2->GetDevice();
                    // Set 0: 1 UBO
                    VkDescriptorSetLayoutBinding b0{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
                    VkDescriptorSetLayoutCreateInfo s0ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,nullptr,0,1,&b0};
                    vkCreateDescriptorSetLayout(dev, &s0ci, nullptr, &s_PbrVk.set0Layout);
                    // Set 1: 5 texture samplers
                    VkDescriptorSetLayoutBinding b1[5]{};
                    for(int i=0;i<5;i++) b1[i]={(uint32_t)i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
                    VkDescriptorSetLayoutCreateInfo s1ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,nullptr,0,5,b1};
                    vkCreateDescriptorSetLayout(dev, &s1ci, nullptr, &s_PbrVk.set1Layout);
                    // Pipeline layout
                    VkDescriptorSetLayout sl[]={s_PbrVk.set0Layout,s_PbrVk.set1Layout};
                    VkPushConstantRange pr{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,256};
                    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,nullptr,0,2,sl,1,&pr};
                    vkCreatePipelineLayout(dev, &plci, nullptr, &s_PbrVk.pipelineLayout);
                    // Pipeline
                    VkPipelineShaderStageCreateInfo ss[2]{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_VERTEX_BIT,vkShader2->GetVertexShaderModule(),"main",nullptr},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_FRAGMENT_BIT,vkShader2->GetFragmentShaderModule(),"main",nullptr}};
                    VkVertexInputBindingDescription vib{0,44,VK_VERTEX_INPUT_RATE_VERTEX};
                    VkVertexInputAttributeDescription via[4]{{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32_SFLOAT,12},{2,0,VK_FORMAT_R32G32_SFLOAT,24},{3,0,VK_FORMAT_R32G32B32_SFLOAT,32}};
                    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,nullptr,0,1,&vib,4,via};
                    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,nullptr,0,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,VK_FALSE};
                    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,nullptr,0,1,nullptr,1,nullptr};
                    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,nullptr,0,VK_FALSE,VK_FALSE,VK_POLYGON_MODE_FILL,VK_CULL_MODE_NONE,VK_FRONT_FACE_COUNTER_CLOCKWISE,VK_FALSE,0,0,0,1.0f};
                    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,nullptr,0,VK_SAMPLE_COUNT_1_BIT,VK_FALSE,0,nullptr,VK_FALSE,VK_FALSE};
                    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,nullptr,0,VK_TRUE,VK_TRUE,VK_COMPARE_OP_LESS,VK_FALSE,VK_FALSE,{},{},0,0};
                    VkPipelineColorBlendAttachmentState cba{VK_FALSE,VK_BLEND_FACTOR_ONE,VK_BLEND_FACTOR_ZERO,VK_BLEND_OP_ADD,VK_BLEND_FACTOR_ONE,VK_BLEND_FACTOR_ZERO,VK_BLEND_OP_ADD,VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
                    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,nullptr,0,VK_FALSE,VK_LOGIC_OP_COPY,1,&cba,{0,0,0,0}};
                    VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
                    VkPipelineDynamicStateCreateInfo dyns{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,nullptr,0,2,dyn};
                    VkFormat cf=VK_FORMAT_R8G8B8A8_UNORM, df=VK_FORMAT_D32_SFLOAT_S8_UINT;
                    VkPipelineRenderingCreateInfo dr{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,nullptr,0,1,&cf,df};
                    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,&dr,0,2,ss,&vi,&ia,nullptr,&vp,&rs,&ms,&ds,&cb,&dyns,s_PbrVk.pipelineLayout,nullptr,0,nullptr,0};
                    if(vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&pci,nullptr,&s_PbrVk.pipeline)==VK_SUCCESS){
                        VkDescriptorPoolSize ps[2]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,20}};
                        VkDescriptorPoolCreateInfo pci2{};
                        pci2.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                        pci2.maxSets=5; pci2.poolSizeCount=2; pci2.pPoolSizes=ps;
                        vkCreateDescriptorPool(dev,&pci2,nullptr,&s_PbrVk.pool);
                        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,nullptr,0,sizeof(PreviewCameraUBO),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
                        VmaAllocationCreateInfo ai{}; ai.usage=VMA_MEMORY_USAGE_AUTO; ai.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT; ai.requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                        VmaAllocationInfo ar{};
                        vmaCreateBuffer(vkCtx2->GetAllocator(),&bi,&ai,&s_PbrVk.uboBuffer,&s_PbrVk.uboAlloc,&ar);
                        s_PbrVk.mappedData=ar.pMappedData;
                        VkDescriptorSetAllocateInfo ai0{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,nullptr,s_PbrVk.pool,1,&s_PbrVk.set0Layout};
                        vkAllocateDescriptorSets(dev,&ai0,&s_PbrVk.set0);
                        VkDescriptorBufferInfo cbi{s_PbrVk.uboBuffer,0,sizeof(PreviewCameraUBO)};
                        VkWriteDescriptorSet w0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,s_PbrVk.set0,0,0,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,nullptr,&cbi,nullptr};
                        vkUpdateDescriptorSets(dev,1,&w0,0,nullptr);
                        // Allocate 3 Set 1 copies (triple-buffered per frame-in-flight)
                        for(int f=0;f<3;f++){
                            VkDescriptorSetAllocateInfo ai1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,nullptr,s_PbrVk.pool,1,&s_PbrVk.set1Layout};
                            vkAllocateDescriptorSets(dev,&ai1,&s_PbrVk.set1[f]);
                            auto vkBlack=std::dynamic_pointer_cast<VulkanTexture2D>(s_PbrBlackTex);
                            if(vkBlack&&vkBlack->GetImageView()&&vkBlack->GetSampler()){
                                VkWriteDescriptorSet w[5]{}; VkDescriptorImageInfo im[5]{};
                                for(int b=0;b<5;b++){im[b].sampler=vkBlack->GetSampler();im[b].imageView=vkBlack->GetImageView();im[b].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; w[b].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w[b].dstSet=s_PbrVk.set1[f];w[b].dstBinding=(uint32_t)b;w[b].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w[b].descriptorCount=1;w[b].pImageInfo=&im[b];}
                                vkUpdateDescriptorSets(dev,5,w,0,nullptr);
                            }
                        }
                        s_PbrVk.initialized=true;
                        AYAYA_CORE_INFO("AssetPreviewer: PBR pipeline created (manual, zero-interference)");
                    }
                }
            }
        }

        AYAYA_CORE_INFO("AssetPreviewer: Ready ({0}), {1}x{1}",
                       RendererAPI::GetAPI() == RendererAPI::API::Vulkan ? "Vulkan" : "OpenGL",
                       kPreviewSize);
    }

    void AssetPreviewer::Shutdown() {
        // Destroy dedicated thumbnail Vulkan resources
        if (s_ThumbnailVk.initialized) {
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            if (vkCtx) {
                VkDevice device = vkCtx->GetDevice();
                VmaAllocator allocator = vkCtx->GetAllocator();
                if (s_ThumbnailVk.pool) vkDestroyDescriptorPool(device, s_ThumbnailVk.pool, nullptr);
                if (s_ThumbnailVk.uboBuffer) vmaDestroyBuffer(allocator, s_ThumbnailVk.uboBuffer, s_ThumbnailVk.uboAllocation);
            }
            s_ThumbnailVk = {};
        }

        s_RealtimeWrapper.reset();
        s_PreviewPipeline.reset();
        s_CameraUBO.reset();
        s_PreviewFBO_MSAA.reset();
        s_PreviewFBO.reset();
        s_PreviewShader.reset();
        s_SphereModel.reset();
        s_LastModelHandle = 0;
        s_ThumbCache.clear();
        if(s_PbrVk.initialized){
            auto vkCtx3=std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
            if(vkCtx3){VkDevice d=vkCtx3->GetDevice();VmaAllocator a=vkCtx3->GetAllocator();
                vkDeviceWaitIdle(d);
                if(s_PbrVk.pipeline)vkDestroyPipeline(d,s_PbrVk.pipeline,nullptr);
                if(s_PbrVk.pipelineLayout)vkDestroyPipelineLayout(d,s_PbrVk.pipelineLayout,nullptr);
                if(s_PbrVk.set0Layout)vkDestroyDescriptorSetLayout(d,s_PbrVk.set0Layout,nullptr);
                if(s_PbrVk.set1Layout)vkDestroyDescriptorSetLayout(d,s_PbrVk.set1Layout,nullptr);
                if(s_PbrVk.pool)vkDestroyDescriptorPool(d,s_PbrVk.pool,nullptr);
                if(s_PbrVk.uboBuffer)vmaDestroyBuffer(a,s_PbrVk.uboBuffer,s_PbrVk.uboAlloc);
            }s_PbrVk={};
        }
        s_PbrBlackTex.reset();
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

            // Write camera data to dedicated thumbnail UBO (isolated from frame loop)
            PreviewCameraUBO camData;
            camData.ViewProjection = s_ProjMatrix * s_ViewMatrix;
            camData.CameraPosition = s_CameraPos;
            memcpy(s_ThumbnailVk.mappedData, &camData, sizeof(PreviewCameraUBO));

            // Push constants
            PreviewPushConstants push;
            glm::vec3 center = ComputeModelCenter(model);
            push.ModelMatrix = glm::translate(glm::mat4(1.0f), -center);
            push.Albedo      = glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
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

            auto vkFBO_MSAA = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO_MSAA);
            if (!vkFBO_MSAA) return nullptr;

            // Dynamic Rendering: FBO starts in SHADER_READ_ONLY (from Invalidate),
            // transition to COLOR_ATTACHMENT before vkCmdBeginRendering
            {
                VkImage colorImg = vkFBO_MSAA->GetColorAttachmentImage(0);
                VkImage depthImg = vkFBO_MSAA->GetDepthAttachmentImage();
                VkImageMemoryBarrier barriers[2]{};
                uint32_t barrierCount = 0;

                barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barriers[barrierCount].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barriers[barrierCount].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barriers[barrierCount].image = colorImg;
                barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barriers[barrierCount].subresourceRange.levelCount = 1;
                barriers[barrierCount].subresourceRange.layerCount = 1;
                barrierCount++;

                if (depthImg != VK_NULL_HANDLE) {
                    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    barriers[barrierCount].srcAccessMask = 0;
                    barriers[barrierCount].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    barriers[barrierCount].image = depthImg;
                    // D32_SFLOAT_S8_UINT 格式必须同时包含 DEPTH + STENCIL aspect
                    barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                    barriers[barrierCount].subresourceRange.levelCount = 1;
                    barriers[barrierCount].subresourceRange.layerCount = 1;
                    barrierCount++;
                }

                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    0, 0, nullptr, 0, nullptr, barrierCount, barriers);
            }

            // === Dynamic Rendering ===
            {
                VkRenderingAttachmentInfo colorAttach{};
                colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                colorAttach.imageView = vkFBO_MSAA->GetColorAttachmentImageView(0);
                colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorAttach.clearValue.color = {{ 0.11f, 0.11f, 0.12f, 1.0f }};

                VkRenderingAttachmentInfo depthAttach{};
                depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depthAttach.imageView = vkFBO_MSAA->GetDepthAttachmentImageView();
                depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                depthAttach.clearValue.depthStencil = { 1.0f, 0 };

                VkRenderingInfo renderingInfo{};
                renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                renderingInfo.renderArea = { {0, 0}, {kPreviewSize, kPreviewSize} };
                renderingInfo.layerCount = 1;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachments = &colorAttach;
                renderingInfo.pDepthAttachment = &depthAttach;

                vkCmdBeginRendering(cmd, &renderingInfo);

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

                // Bind dedicated thumbnail descriptor set (isolated from frame loop)
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    vkPipeline->GetVulkanPipelineLayout(), 0, 1,
                    &s_ThumbnailVk.descriptorSet, 0, nullptr);

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
                vkCmdEndRendering(cmd);
            }

            // === MSAA resolve: s_PreviewFBO_MSAA → s_PreviewFBO ===
            {
                VkImage srcImage = vkFBO_MSAA->GetColorAttachmentImage(0);
                VkImage dstImage = vkFBO->GetColorAttachmentImage(0);

                VkImageMemoryBarrier preBarriers[2]{};
                // MSAA src: dynamic rendering leaves it in COLOR_ATTACHMENT_OPTIMAL
                preBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                preBarriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                preBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                preBarriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                preBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                preBarriers[0].image = srcImage;
                preBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                preBarriers[0].subresourceRange.levelCount = 1;
                preBarriers[0].subresourceRange.layerCount = 1;

                // Resolve dst: still in SHADER_READ_ONLY_OPTIMAL (not yet rendered to)
                preBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                preBarriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                preBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                preBarriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                preBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                preBarriers[1].image = dstImage;
                preBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                preBarriers[1].subresourceRange.levelCount = 1;
                preBarriers[1].subresourceRange.layerCount = 1;

                // srcStage: COLOR_ATTACHMENT_WRITE (barrier[0]) + SHADER_READ (barrier[1])
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 2, preBarriers);

                VkImageResolve resolveRegion{};
                resolveRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                resolveRegion.srcSubresource.layerCount = 1;
                resolveRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                resolveRegion.dstSubresource.layerCount = 1;
                resolveRegion.extent = { kPreviewSize, kPreviewSize, 1 };
                vkCmdResolveImage(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  1, &resolveRegion);

                // Transition resolve target to TRANSFER_SRC for the copy-to-buffer below
                VkImageMemoryBarrier postBarrier{};
                postBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                postBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                postBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                postBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                postBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                postBarrier.image = dstImage;
                postBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                postBarrier.subresourceRange.levelCount = 1;
                postBarrier.subresourceRange.layerCount = 1;

                // Transition MSAA source back to SHADER_READ_ONLY
                preBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                preBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                preBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                preBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                // preBarriers[1] is replaced by postBarrier below
                VkImageMemoryBarrier postBarriers[2] = { preBarriers[0], postBarrier };

                // dstStage: TRANSFER_READ (barrier[1]) + SHADER_READ (barrier[0])
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 2, postBarriers);
            }

            // === Copy resolved image to staging buffer (image already in TRANSFER_SRC from resolve step) ===
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
            // FBO was rendered with flipped viewport — data is already correctly oriented
            if (auto vkTex = std::dynamic_pointer_cast<VulkanTexture2D>(tex))
                vkTex->SetDataFlipped(false);
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

    // ---- Material thumbnail: render a sphere with the material applied ----
    std::shared_ptr<Texture2D> AssetPreviewer::GenerateThumbnailForMaterial(UUID materialHandle, uint32_t size) {
        (void)size;
        auto material = AssetManager::GetAsset<Material>(materialHandle);
        if (!material) return nullptr;

        // Create a sphere mesh wrapped in a Model (cached globally)
        if (!s_SphereModel) {
            auto sphereMesh = Mesh::CreateSphere(0.5f, 64, 64);
            s_SphereModel = std::make_shared<Model>(sphereMesh);
        }
        if (s_SphereModel->GetMeshes().empty()) return nullptr;

        AutoFrameCamera(s_SphereModel, glm::vec2(0.3f, -0.6f), glm::radians(45.0f));

        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            auto vkFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO);
            auto vkFBO_MSAA = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO_MSAA);
            auto vkPipeline = std::dynamic_pointer_cast<VulkanPipeline>(s_PreviewPipeline);
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            if (!vkFBO || !vkFBO_MSAA || !vkPipeline || !vkCtx) return nullptr;

            // Camera UBO
            PreviewCameraUBO camData;
            camData.ViewProjection = s_ProjMatrix * s_ViewMatrix;
            camData.CameraPosition = s_CameraPos;
            memcpy(s_ThumbnailVk.mappedData, &camData, sizeof(PreviewCameraUBO));

            // Push constants
            PreviewPushConstants push{};
            push.ModelMatrix = glm::translate(glm::mat4(1.0f), -ComputeModelCenter(s_SphereModel));
            push.Albedo      = glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
            push.LightDir    = glm::vec4(0.5f, 1.0f, 0.8f, 0.0f);
            push.LightColor  = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);
            push.Ambient     = glm::vec4(0.15f, 0.15f, 0.17f, 1.0f);
            push.UseAlbedoMap = 0;

            // Read material albedo color
            for (auto& prop : material->Properties) {
                if (prop.UniformName == "u_Albedo" && prop.Type == MaterialPropertyType::Vec3)
                    push.Albedo = glm::vec4(prop.Vec3Value, 1.0f);
            }

            // Staging buffer
            VmaAllocator allocator = vkCtx->GetAllocator();
            uint32_t dataSize = kPreviewSize * kPreviewSize * 4;
            VkBuffer stagingBuf; VmaAllocation stagingAlloc;
            {
                VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bi.size = dataSize; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                VmaAllocationCreateInfo ai{}; ai.usage = VMA_MEMORY_USAGE_AUTO;
                ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
                vmaCreateBuffer(allocator, &bi, &ai, &stagingBuf, &stagingAlloc, nullptr);
            }

            VkCommandBuffer cmd = vkCtx->BeginSingleTimeCommands();

            // Transition FBO to COLOR_ATTACHMENT
            {
                VkImage colorImg = vkFBO_MSAA->GetColorAttachmentImage(0);
                VkImage depthImg = vkFBO_MSAA->GetDepthAttachmentImage();
                VkImageMemoryBarrier barriers[2]{}; uint32_t bc = 0;
                barriers[bc].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[bc].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barriers[bc].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barriers[bc].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barriers[bc].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barriers[bc].image = colorImg;
                barriers[bc].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barriers[bc].subresourceRange.levelCount = 1;
                barriers[bc].subresourceRange.layerCount = 1; bc++;
                if (depthImg) {
                    barriers[bc].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barriers[bc].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barriers[bc].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    barriers[bc].srcAccessMask = 0;
                    barriers[bc].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    barriers[bc].image = depthImg;
                    barriers[bc].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                    barriers[bc].subresourceRange.levelCount = 1;
                    barriers[bc].subresourceRange.layerCount = 1; bc++;
                }
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    0, 0, nullptr, 0, nullptr, bc, barriers);
            }

            // Dynamic Rendering
            {
                VkRenderingAttachmentInfo colorAttach{};
                colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                colorAttach.imageView = vkFBO_MSAA->GetColorAttachmentImageView(0);
                colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorAttach.clearValue.color = {{ 0.11f, 0.11f, 0.12f, 1.0f }};
                VkRenderingAttachmentInfo depthAttach{};
                depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depthAttach.imageView = vkFBO_MSAA->GetDepthAttachmentImageView();
                depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                depthAttach.clearValue.depthStencil = { 1.0f, 0 };
                VkRenderingInfo ri{};
                ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                ri.renderArea = {{0,0},{kPreviewSize,kPreviewSize}};
                ri.layerCount = 1; ri.colorAttachmentCount = 1;
                ri.pColorAttachments = &colorAttach;
                ri.pDepthAttachment = &depthAttach;
                vkCmdBeginRendering(cmd, &ri);
                VkViewport vp{}; vp.x=0; vp.y=(float)kPreviewSize;
                vp.width=(float)kPreviewSize; vp.height=-(float)kPreviewSize;
                vp.minDepth=0; vp.maxDepth=1;
                vkCmdSetViewport(cmd,0,1,&vp);
                VkRect2D scissor{}; scissor.extent={kPreviewSize,kPreviewSize};
                vkCmdSetScissor(cmd,0,1,&scissor);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->GetVulkanPipeline());
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    vkPipeline->GetVulkanPipelineLayout(), 0, 1,
                    &s_ThumbnailVk.descriptorSet, 0, nullptr);

                for (auto& mesh : s_SphereModel->GetMeshes()) {
                    vkCmdPushConstants(cmd, vkPipeline->GetVulkanPipelineLayout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(PreviewPushConstants), &push);
                    auto vkVB = std::dynamic_pointer_cast<VulkanVertexBuffer>(mesh->GetVertexBuffer());
                    auto vkIB = std::dynamic_pointer_cast<VulkanIndexBuffer>(mesh->GetIndexBuffer());
                    if (vkVB && vkIB) {
                        VkBuffer vbs[]={vkVB->GetVulkanBuffer()};
                        VkDeviceSize off[]={0};
                        vkCmdBindVertexBuffers(cmd,0,1,vbs,off);
                        vkCmdBindIndexBuffer(cmd, vkIB->GetVulkanBuffer(), 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(cmd, mesh->GetIndexCount(), 1, 0, 0, 0);
                    }
                }
                vkCmdEndRendering(cmd);
            }

            // MSAA resolve + readback (reuse same pattern as GenerateThumbnail)
            {
                VkImage srcImg = vkFBO_MSAA->GetColorAttachmentImage(0);
                VkImage dstImg = vkFBO->GetColorAttachmentImage(0);
                VkImageMemoryBarrier pre[2]{};
                pre[0].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                pre[0].oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                pre[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                pre[0].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                pre[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                pre[0].image=srcImg;
                pre[0].subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
                pre[0].subresourceRange.levelCount=1;pre[0].subresourceRange.layerCount=1;
                pre[1].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                pre[1].oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                pre[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                pre[1].srcAccessMask=VK_ACCESS_SHADER_READ_BIT;
                pre[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                pre[1].image=dstImg;
                pre[1].subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
                pre[1].subresourceRange.levelCount=1;pre[1].subresourceRange.layerCount=1;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0,0,nullptr,0,nullptr,2,pre);
                VkImageResolve res{};
                res.srcSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;res.srcSubresource.layerCount=1;
                res.dstSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;res.dstSubresource.layerCount=1;
                res.extent={kPreviewSize,kPreviewSize,1};
                vkCmdResolveImage(cmd,srcImg,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    dstImg,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&res);
                // Transition dst to TRANSFER_SRC for readback, src back to SHADER_READ_ONLY
                VkImageMemoryBarrier post[2]{};
                post[0].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                post[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                post[0].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                post[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                post[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                post[0].image=srcImg;
                post[0].subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
                post[0].subresourceRange.levelCount=1;post[0].subresourceRange.layerCount=1;
                post[1].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                post[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                post[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                post[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                post[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                post[1].image=dstImg;
                post[1].subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
                post[1].subresourceRange.levelCount=1;post[1].subresourceRange.layerCount=1;
                vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0,0,nullptr,0,nullptr,2,post);
                VkBufferImageCopy region{};
                region.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.layerCount=1;
                region.imageExtent={kPreviewSize,kPreviewSize,1};
                vkCmdCopyImageToBuffer(cmd,dstImg,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,stagingBuf,1,&region);
                VkImageMemoryBarrier back{};
                back.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                back.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                back.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                back.image=dstImg;
                back.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
                back.subresourceRange.levelCount=1;back.subresourceRange.layerCount=1;
                back.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                back.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0,0,nullptr,0,nullptr,1,&back);
            }

            vkCtx->EndSingleTimeCommands(cmd);
            void* mapped = nullptr;
            vmaMapMemory(allocator, stagingAlloc, &mapped);
            auto tex = Texture2D::Create(kPreviewSize, kPreviewSize);
            if (tex && mapped) tex->SetData(mapped, dataSize);
            if (auto vkTex = std::dynamic_pointer_cast<VulkanTexture2D>(tex))
                vkTex->SetDataFlipped(false);
            vmaUnmapMemory(allocator, stagingAlloc);
            vmaDestroyBuffer(allocator, stagingBuf, stagingAlloc);
            return tex;
        }

        // OpenGL fallback
        RenderModel(s_SphereModel);
        return ReadbackStandalone();
    }

    // ---- Prefab thumbnail: render the prefab's first mesh entity ----
    std::shared_ptr<Texture2D> AssetPreviewer::GenerateThumbnailForPrefab(UUID prefabHandle, uint32_t size) {
        auto prefab = AssetManager::GetAsset<Prefab>(prefabHandle);
        if (!prefab) return nullptr;

        Scene* scene = prefab->GetScene();
        if (!scene) return nullptr;

        // Find the first entity with a MeshRendererComponent that references a valid model
        auto view = scene->Reg().view<MeshRendererComponent>();
        for (auto entityID : view) {
            auto& mrc = view.get<MeshRendererComponent>(entityID);
            if (mrc.ModelHandle != 0) {
                auto model = AssetManager::GetAsset<Model>(mrc.ModelHandle);
                if (model && !model->GetMeshes().empty())
                    return GenerateThumbnail(mrc.ModelHandle, size);
            }
        }
        return nullptr;
    }

    // ---- Prefab realtime preview: render ALL mesh entities with their transforms ----
    std::shared_ptr<Texture2D> AssetPreviewer::RenderRealtimePreviewForPrefab(UUID prefabHandle, glm::vec2 cameraAngle, uint32_t size) {
        (void)size;
        auto prefab = AssetManager::GetAsset<Prefab>(prefabHandle);
        if (!prefab) return nullptr;

        Scene* scene = prefab->GetScene();
        if (!scene) return nullptr;

        // Collect all (worldTransform, Mesh) pairs from all mesh entities
        struct DrawItem {
            glm::mat4 Transform;
            std::shared_ptr<Mesh> MeshAsset;
        };
        std::vector<DrawItem> drawList;

        glm::vec3 combinedMin( FLT_MAX), combinedMax(-FLT_MAX);
        bool hasAny = false;

        std::function<void(entt::entity, const glm::mat4&)> collectRecursive;
        collectRecursive = [&collectRecursive, &drawList, &combinedMin, &combinedMax, &hasAny, scene](entt::entity e, const glm::mat4& parentWorld) {
            Entity ent{ e, scene };
            if (!ent) return;

            glm::mat4 world = parentWorld;
            if (ent.HasComponent<TransformComponent>())
                world = parentWorld * ent.GetComponent<TransformComponent>().GetTransform();

            if (ent.HasComponent<MeshRendererComponent>()) {
                auto& mrc = ent.GetComponent<MeshRendererComponent>();
                if (mrc.ModelHandle != 0) {
                    auto model = AssetManager::GetAsset<Model>(mrc.ModelHandle);
                    if (model) {
                        for (auto& mesh : model->GetMeshes()) {
                            if (mesh->GetIndexCount() == 0) continue;
                            drawList.push_back({ world, mesh });
                            const AABB& box = mesh->GetAABB();
                            // Transform AABB corners to world space for combined bounds
                            glm::vec4 corners[8] = {
                                world * glm::vec4(box.Min.x, box.Min.y, box.Min.z, 1.0f),
                                world * glm::vec4(box.Max.x, box.Min.y, box.Min.z, 1.0f),
                                world * glm::vec4(box.Min.x, box.Max.y, box.Min.z, 1.0f),
                                world * glm::vec4(box.Max.x, box.Max.y, box.Min.z, 1.0f),
                                world * glm::vec4(box.Min.x, box.Min.y, box.Max.z, 1.0f),
                                world * glm::vec4(box.Max.x, box.Min.y, box.Max.z, 1.0f),
                                world * glm::vec4(box.Min.x, box.Max.y, box.Max.z, 1.0f),
                                world * glm::vec4(box.Max.x, box.Max.y, box.Max.z, 1.0f),
                            };
                            for (auto& c : corners) {
                                combinedMin = glm::min(combinedMin, glm::vec3(c));
                                combinedMax = glm::max(combinedMax, glm::vec3(c));
                            }
                            hasAny = true;
                        }
                    }
                }
            }

            if (ent.HasComponent<RelationshipComponent>()) {
                for (auto child : ent.GetComponent<RelationshipComponent>().Children)
                    collectRecursive(child, world);
            }
        };

        for (auto rootHandle : scene->GetRootEntities())
            collectRecursive(rootHandle, glm::mat4(1.0f));

        if (!hasAny || drawList.empty()) return nullptr;

        // Compute combined center and bounding radius for auto-framing
        glm::vec3 center = (combinedMin + combinedMax) * 0.5f;
        float radius = glm::length(combinedMax - combinedMin) * 0.5f;
        if (radius < 0.001f) radius = 0.5f;

        // Auto-frame camera using combined bounds
        float fovY = glm::radians(45.0f);
        float distance = radius / glm::sin(fovY * 0.5f);
        if (distance < radius * 1.2f) distance = radius * 1.5f;

        float pitch = glm::clamp(cameraAngle.x, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
        float yaw   = cameraAngle.y;

        glm::vec3 camPos;
        camPos.x = distance * glm::cos(pitch) * glm::sin(yaw);
        camPos.y = distance * glm::sin(pitch);
        camPos.z = distance * glm::cos(pitch) * glm::cos(yaw);
        camPos += center;

        glm::mat4 viewMat = glm::lookAt(camPos, center, glm::vec3(0.0f, 1.0f, 0.0f));
        float nearClip = glm::max(distance * 0.001f, 0.01f);
        float farClip  = glm::max(distance * 10.0f, 100.0f);
        glm::mat4 projMat = glm::perspective(fovY, 1.0f, nearClip, farClip);

        s_CameraPos = camPos;
        s_ViewMatrix = viewMat;
        s_ProjMatrix = projMat;

        // --- Vulkan path ---
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            if (!vkCtx) return nullptr;

            PreviewCameraUBO camData;
            camData.ViewProjection = s_ProjMatrix * s_ViewMatrix;
            camData.CameraPosition = s_CameraPos;
            s_CameraUBO->SetData(&camData, sizeof(PreviewCameraUBO));

            PreviewPushConstants push;
            push.Albedo      = glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
            push.LightDir    = glm::vec4(glm::normalize(glm::vec3(0.5f, 1.0f, 0.8f)), 0.0f);
            push.LightColor  = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);
            push.Ambient     = glm::vec4(0.15f, 0.15f, 0.17f, 1.0f);

            auto cmd = RenderCommandBuffer::Create();
            if (!cmd) return nullptr;
            cmd->Begin();

            // FBO transition to attachment
            {
                auto vkMSAA = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO_MSAA);
                if (vkMSAA) {
                    VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
                    VkImage colorImg = vkMSAA->GetColorAttachmentImage(0);
                    VkImage depthImg = vkMSAA->GetDepthAttachmentImage();
                    VkImageMemoryBarrier barriers[2]{}; uint32_t bc = 0;
                    barriers[bc].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barriers[bc].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barriers[bc].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    barriers[bc].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    barriers[bc].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    barriers[bc].image = colorImg;
                    barriers[bc].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    barriers[bc].subresourceRange.levelCount = 1;
                    barriers[bc].subresourceRange.layerCount = 1; bc++;
                    if (depthImg != VK_NULL_HANDLE) {
                        barriers[bc].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barriers[bc].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        barriers[bc].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                        barriers[bc].srcAccessMask = 0;
                        barriers[bc].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                        barriers[bc].image = depthImg;
                        barriers[bc].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                        barriers[bc].subresourceRange.levelCount = 1;
                        barriers[bc].subresourceRange.layerCount = 1; bc++;
                    }
                    vkCmdPipelineBarrier(vkCmd,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        0, 0, nullptr, 0, nullptr, bc, barriers);
                }
            }

            cmd->BeginRenderPass(s_PreviewFBO_MSAA, true, glm::vec4(0.11f, 0.11f, 0.12f, 1.0f));
            cmd->BindPipeline(s_PreviewPipeline);

            // Render ALL meshes with per-entity transforms
            for (auto& item : drawList) {
                push.ModelMatrix = item.Transform;
                cmd->PushConstantData(s_PreviewPipeline, &push, sizeof(push));
                cmd->DrawIndexed(item.MeshAsset, item.MeshAsset->GetIndexCount());
            }

            cmd->EndRenderPass();

            // MSAA resolve
            {
                auto vkMSAA = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO_MSAA);
                auto vkResolve = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO);
                if (vkMSAA && vkResolve) {
                    VkCommandBuffer cb = vkCtx->GetCurrentCommandBuffer();
                    VkImage srcImg = vkMSAA->GetColorAttachmentImage(0);
                    VkImage dstImg = vkResolve->GetColorAttachmentImage(0);
                    VkImageMemoryBarrier pre[2]{};
                    pre[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    pre[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    pre[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    pre[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    pre[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    pre[0].image = srcImg;
                    pre[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    pre[0].subresourceRange.levelCount = 1;
                    pre[0].subresourceRange.layerCount = 1;
                    pre[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    pre[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    pre[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    pre[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    pre[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    pre[1].image = dstImg;
                    pre[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    pre[1].subresourceRange.levelCount = 1;
                    pre[1].subresourceRange.layerCount = 1;
                    vkCmdPipelineBarrier(cb,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 2, pre);
                    VkImageResolve res{};
                    res.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    res.srcSubresource.layerCount = 1;
                    res.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    res.dstSubresource.layerCount = 1;
                    res.extent = { kPreviewSize, kPreviewSize, 1 };
                    vkCmdResolveImage(cb, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                      dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &res);
                    VkImageMemoryBarrier post[2]{};
                    post[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    post[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    post[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    post[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    post[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    post[0].image = srcImg;
                    post[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    post[0].subresourceRange.levelCount = 1;
                    post[0].subresourceRange.layerCount = 1;
                    post[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    post[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    post[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    post[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    post[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    post[1].image = dstImg;
                    post[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    post[1].subresourceRange.levelCount = 1;
                    post[1].subresourceRange.layerCount = 1;
                    vkCmdPipelineBarrier(cb,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 2, post);
                }
            }
            cmd->End();
            return ReadbackRealtime();
        }

        // --- OpenGL path ---
        GLuint fboMSAA  = (GLuint)(uintptr_t)s_PreviewFBO_MSAA->GetRendererID();
        GLuint fboResolve = (GLuint)(uintptr_t)s_PreviewFBO->GetRendererID();
        GLint prevFBO = 0, prevVP[4];
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
        glGetIntegerv(GL_VIEWPORT, prevVP);
        GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);

        glBindFramebuffer(GL_FRAMEBUFFER, fboMSAA);
        glViewport(0, 0, kPreviewSize, kPreviewSize);
        glClearColor(0.11f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        s_PreviewShader->Bind();
        glm::mat4 viewProj = s_ProjMatrix * s_ViewMatrix;
        s_PreviewShader->SetMat4("u_ViewProjection", viewProj);
        s_PreviewShader->SetFloat3("u_CameraPos", s_CameraPos);

        for (auto& item : drawList) {
            glm::mat4 normalMat = glm::transpose(glm::inverse(glm::mat3(item.Transform)));
            s_PreviewShader->SetMat4("u_Transform", item.Transform);
            s_PreviewShader->SetMat4("u_NormalMatrix", glm::mat4(normalMat));
            auto va = item.MeshAsset->GetVertexArray();
            if (!va) continue;
            va->Bind();
            glDrawElements(GL_TRIANGLES, (GLsizei)item.MeshAsset->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        }

        glBindVertexArray(0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fboMSAA);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboResolve);
        glBlitFramebuffer(0, 0, kPreviewSize, kPreviewSize, 0, 0, kPreviewSize, kPreviewSize,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
        if (!prevDepth) glDisable(GL_DEPTH_TEST);

        return ReadbackRealtime();
    }

    // ---- GPU-resident thumbnail API (zero CPU blocking, zero readback) ----

    void AssetPreviewer::RequestThumbnail(UUID handle, int assetType) {
        if(s_ThumbCache.count(handle)) return;
        for(auto& r:s_ThumbQueue) if(r.handle==handle) return;
        s_ThumbQueue.push_back({handle,assetType});
    }

    void AssetPreviewer::ProcessOneThumbnail() {
        if(s_ThumbQueue.empty()||RendererAPI::GetAPI()!=RendererAPI::API::Vulkan) return;
        auto vkCtx=std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        auto vkPipe=std::dynamic_pointer_cast<VulkanPipeline>(s_PreviewPipeline);
        if(!vkCtx||!vkPipe) return;
        VkCommandBuffer cb=vkCtx->GetCurrentCommandBuffer();

        auto& r=s_ThumbQueue.front();
        uint32_t fi = vkCtx->GetCurrentFrameIndex() % 3;
        std::shared_ptr<Model> mdl; bool usePBR=false;
        PBRPush pbr{}; PreviewPushConstants wm{};

        // Prepare model + camera + push constants.
        // For SubAsset UUIDs (single mesh), resolve to parent Model with ALL meshes.
        UUID modelHandle = r.handle;
        std::shared_ptr<Material> prefabMat;  // material for Prefab PBR preview
        if(r.assetType==1){
            auto pf=AssetManager::GetAsset<Prefab>(r.handle);
            if(pf&&pf->GetScene()){auto v=pf->GetScene()->Reg().view<MeshRendererComponent>();for(auto e:v){auto&mc=v.get<MeshRendererComponent>(e);if(mc.ModelHandle){modelHandle=mc.ModelHandle;prefabMat=(mc.MaterialHandle!=0)?AssetManager::GetAsset<Material>(mc.MaterialHandle):nullptr;break;}}}
        }
        // Check if this is a SubAsset → use parent for full mesh rendering
        {
            auto meta = AssetManager::GetMetadata(modelHandle);
            if (meta.Type == AssetType::SubMesh && meta.ParentHandle != 0)
                modelHandle = meta.ParentHandle;
        }
        if(r.assetType==0||r.assetType==1){
            mdl=AssetManager::GetAsset<Model>(modelHandle);
            if(!mdl||mdl->GetMeshes().empty()){s_ThumbQueue.erase(s_ThumbQueue.begin());return;}
            AutoFrameCamera(mdl,glm::vec2(0.3f,-0.6f),glm::radians(45.0f));
            glm::mat4 modelMat=glm::translate(glm::mat4(1.0f),-ComputeModelCenter(mdl))
                             * mdl->GetRootNode().LocalTransform;
            // Prefab PBR path: only if material textures are already GPU-resident.
            // Force-loading during CB recording would corrupt state.
            if(r.assetType==1 && s_PbrVk.initialized && prefabMat){
                prefabMat->BakeProperties(); auto& pBpc=prefabMat->GetBakedPC();
                // Check if at least Albedo texture is loaded (indicates textures are GPU-ready)
                bool texReady = pBpc.AlbedoMapIndex != 1;  // non-default albedo texture present
                if(texReady){
                    usePBR=true;
                    pbr.ModelMatrix=modelMat; pbr.Albedo=glm::vec4(0.6f,0.6f,0.6f,1.0f); pbr.Roughness=0.5f; pbr.AO=1.0f; pbr.Alpha=1.0f; pbr.AlphaCutoff=0.5f;
                    for(auto& p:prefabMat->Properties){if(p.UniformName=="u_Albedo"&&p.Type==MaterialPropertyType::Vec3)pbr.Albedo=glm::vec4(p.Vec3Value,1.0f); else if(p.UniformName=="u_Metallic"&&p.Type==MaterialPropertyType::Float)pbr.Metallic=p.FloatValue; else if(p.UniformName=="u_Roughness"&&p.Type==MaterialPropertyType::Float)pbr.Roughness=p.FloatValue; else if(p.UniformName=="u_AO"&&p.Type==MaterialPropertyType::Float)pbr.AO=p.FloatValue; else if(p.UniformName=="u_Alpha"&&p.Type==MaterialPropertyType::Float)pbr.Alpha=p.FloatValue; else if(p.UniformName=="u_AlphaCutoff"&&p.Type==MaterialPropertyType::Float)pbr.AlphaCutoff=p.FloatValue; else if(p.UniformName=="u_BlendMode"&&p.Type==MaterialPropertyType::Int)pbr.BlendMode=p.IntValue;}
                    bool hasAlbedo=pBpc.AlbedoMapIndex!=1, hasNormal=pBpc.NormalMapIndex!=3, hasORM=pBpc.UseORMMap!=0;
                    bool hasMetallic=pBpc.MetallicMapIndex!=1, hasRoughness=pBpc.RoughnessMapIndex!=1, hasAO=pBpc.AOMapIndex!=1;
                    pbr.UseAlbedoMap=hasAlbedo?1:0; pbr.UseNormalMap=hasNormal?1:0; pbr.UseORMMap=hasORM?1:0;
                    pbr.UseMetallicMap=hasORM?0:(hasMetallic?1:0); pbr.UseRoughnessMap=hasORM?0:(hasRoughness?1:0); pbr.UseAOMap=hasORM?0:(hasAO?1:0);
                    // Resolve textures from Material Properties for traditional Set=1 binding
                    auto getTex=[&](const char* name)->std::shared_ptr<Texture2D>{
                        for(auto&p:prefabMat->Properties){if(p.UniformName==name&&p.Type==MaterialPropertyType::Texture2D){auto t=p.RuntimeTexture;if(!t&&p.TextureHandle!=0&&AssetManager::IsAssetHandleValid(p.TextureHandle))t=AssetManager::GetAsset<Texture2D>(p.TextureHandle);return t;}} return nullptr;
                    };
                    VkWriteDescriptorSet w[5]{}; VkDescriptorImageInfo im[5]{}; int wc=0;
                    auto addT=[&](int b,std::shared_ptr<Texture2D> t,std::shared_ptr<Texture2D> fb){
                        auto u=t&&t->GetRendererID()?t:fb; if(!u)return; auto vk=std::dynamic_pointer_cast<VulkanTexture2D>(u); if(!vk||!vk->GetImageView()||!vk->GetSampler())return;
                        im[wc].sampler=vk->GetSampler();im[wc].imageView=vk->GetImageView();im[wc].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        w[wc].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w[wc].dstSet=s_PbrVk.set1[fi];w[wc].dstBinding=(uint32_t)b;w[wc].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w[wc].descriptorCount=1;w[wc].pImageInfo=&im[wc]; wc++;
                    };
                    addT(0,getTex("u_AlbedoMap"),s_PbrBlackTex);addT(1,getTex("u_NormalMap"),s_PbrBlackTex);addT(2,getTex("u_ORMMap"),s_PbrBlackTex);addT(3,getTex("u_MetallicMap"),s_PbrBlackTex);addT(4,getTex("u_RoughnessMap"),s_PbrBlackTex);
                    if(wc)vkUpdateDescriptorSets(vkCtx->GetDevice(),wc,w,0,nullptr);
                }
            }
            if(!usePBR){
                wm.ModelMatrix=modelMat;
                wm.Albedo=glm::vec4(0.6f,0.6f,0.6f,1.0f);
            }
        }else{
            auto mat=AssetManager::GetAsset<Material>(r.handle); if(!mat){s_ThumbQueue.erase(s_ThumbQueue.begin());return;}
            if(!s_SphereModel){auto sm=Mesh::CreateSphere(0.5f,64,64);s_SphereModel=std::make_shared<Model>(sm);}
            AutoFrameCamera(s_SphereModel,glm::vec2(0.3f,-0.6f),glm::radians(45.0f)); mdl=s_SphereModel;
            if(s_PbrVk.initialized){usePBR=true;
                pbr.ModelMatrix=glm::translate(glm::mat4(1.0f),-ComputeModelCenter(s_SphereModel))
                              * s_SphereModel->GetRootNode().LocalTransform;
                pbr.Albedo=glm::vec4(0.6f,0.6f,0.6f,1.0f); pbr.Roughness=0.5f; pbr.AO=1.0f; pbr.Alpha=1.0f; pbr.AlphaCutoff=0.5f;
                mat->BakeProperties(); auto&bpc=mat->GetBakedPC();
                for(auto&p:mat->Properties){if(p.UniformName=="u_Albedo"&&p.Type==MaterialPropertyType::Vec3)pbr.Albedo=glm::vec4(p.Vec3Value,1.0f); else if(p.UniformName=="u_Metallic"&&p.Type==MaterialPropertyType::Float)pbr.Metallic=p.FloatValue; else if(p.UniformName=="u_Roughness"&&p.Type==MaterialPropertyType::Float)pbr.Roughness=p.FloatValue; else if(p.UniformName=="u_AO"&&p.Type==MaterialPropertyType::Float)pbr.AO=p.FloatValue; else if(p.UniformName=="u_Alpha"&&p.Type==MaterialPropertyType::Float)pbr.Alpha=p.FloatValue; else if(p.UniformName=="u_AlphaCutoff"&&p.Type==MaterialPropertyType::Float)pbr.AlphaCutoff=p.FloatValue; else if(p.UniformName=="u_BlendMode"&&p.Type==MaterialPropertyType::Int)pbr.BlendMode=p.IntValue;}
                bool hasAlbedo=bpc.AlbedoMapIndex!=1, hasNormal=bpc.NormalMapIndex!=3, hasORM=bpc.UseORMMap!=0;
                bool hasMetallic=bpc.MetallicMapIndex!=1, hasRoughness=bpc.RoughnessMapIndex!=1, hasAO=bpc.AOMapIndex!=1;
                pbr.UseAlbedoMap=hasAlbedo?1:0; pbr.UseNormalMap=hasNormal?1:0; pbr.UseORMMap=hasORM?1:0;
                pbr.UseMetallicMap=hasORM?0:(hasMetallic?1:0); pbr.UseRoughnessMap=hasORM?0:(hasRoughness?1:0); pbr.UseAOMap=hasORM?0:(hasAO?1:0);
                // Resolve textures from Material Properties for traditional Set=1 binding
                auto getTex=[&](const char* name)->std::shared_ptr<Texture2D>{
                    for(auto&p:mat->Properties){if(p.UniformName==name&&p.Type==MaterialPropertyType::Texture2D){auto t=p.RuntimeTexture;if(!t&&p.TextureHandle!=0&&AssetManager::IsAssetHandleValid(p.TextureHandle))t=AssetManager::GetAsset<Texture2D>(p.TextureHandle);return t;}} return nullptr;
                };
                VkWriteDescriptorSet w[5]{}; VkDescriptorImageInfo im[5]{}; int wc=0;
                auto addT=[&](int b,std::shared_ptr<Texture2D> t,std::shared_ptr<Texture2D> fb){
                    auto u=t&&t->GetRendererID()?t:fb; if(!u)return; auto vk=std::dynamic_pointer_cast<VulkanTexture2D>(u); if(!vk||!vk->GetImageView()||!vk->GetSampler())return;
                    im[wc].sampler=vk->GetSampler();im[wc].imageView=vk->GetImageView();im[wc].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    w[wc].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w[wc].dstSet=s_PbrVk.set1[fi];w[wc].dstBinding=(uint32_t)b;w[wc].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w[wc].descriptorCount=1;w[wc].pImageInfo=&im[wc]; wc++;
                };
                addT(0,getTex("u_AlbedoMap"),s_PbrBlackTex);addT(1,getTex("u_NormalMap"),s_PbrBlackTex);addT(2,getTex("u_ORMMap"),s_PbrBlackTex);addT(3,getTex("u_MetallicMap"),s_PbrBlackTex);addT(4,getTex("u_RoughnessMap"),s_PbrBlackTex);
                if(wc)vkUpdateDescriptorSets(vkCtx->GetDevice(),wc,w,0,nullptr);
            }else{
                wm.ModelMatrix=glm::translate(glm::mat4(1.0f),-ComputeModelCenter(s_SphereModel))
                             * s_SphereModel->GetRootNode().LocalTransform;
                wm.Albedo=glm::vec4(0.6f,0.6f,0.6f,1.0f);
                for(auto&p:mat->Properties)if(p.UniformName=="u_Albedo"&&p.Type==MaterialPropertyType::Vec3)wm.Albedo=glm::vec4(p.Vec3Value,1.0f);
            }
        }
        wm.LightDir=glm::vec4(0.5f,1.0f,0.8f,0.0f);wm.LightColor=glm::vec4(1.0f,0.98f,0.95f,1.0f);wm.Ambient=glm::vec4(0.15f,0.15f,0.17f,1.0f);
        PreviewCameraUBO cam; cam.ViewProjection=s_ProjMatrix*s_ViewMatrix; cam.CameraPosition=s_CameraPos;
        if(usePBR)memcpy(s_PbrVk.mappedData,&cam,sizeof(cam)); else memcpy(s_ThumbnailVk.mappedData,&cam,sizeof(cam));

        // Create per-thumbnail texture. Texture2D::Create just allocates VkImage —
        // no EndSingleTimeCommands, safe to call during frame recording.
        auto dstTex=Texture2D::Create(kPreviewSize,kPreviewSize);
        auto vkDst=std::dynamic_pointer_cast<VulkanTexture2D>(dstTex);
        // Viewport Y-flip already produces upright image; tell ImGui not to flip UVs again
        if(vkDst) vkDst->SetDataFlipped(false);
        if(!vkDst||!vkDst->GetImageView()||!vkDst->GetImage()){s_ThumbQueue.erase(s_ThumbQueue.begin());return;}
        VkImage dstImg=vkDst->GetImage();

        // Transition destination to TRANSFER_DST
        {
            VkImageMemoryBarrier b{};
            b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcAccessMask=0; b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            b.image=dstImg; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        }

        // Render into appropriate FBO (PBR=1-sample, white-model=4-sample MSAA)
        {
            auto renderFbo = usePBR ? s_PreviewFBO : s_PreviewFBO_MSAA;
            auto vkRenderFbo = std::dynamic_pointer_cast<VulkanFramebuffer>(renderFbo);
            if(!vkRenderFbo){s_ThumbQueue.erase(s_ThumbQueue.begin());return;}
            VkImage renderImg=vkRenderFbo->GetColorAttachmentImage(0);

            // Transition render target to COLOR_ATTACHMENT + DEPTH_ATTACHMENT
            VkImage depthImg=vkRenderFbo->GetDepthAttachmentImage();
            VkImageMemoryBarrier barriers[2]{};
            uint32_t barrierCount=0;
            barriers[barrierCount].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[barrierCount].oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[barrierCount].newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[barrierCount].srcAccessMask=VK_ACCESS_SHADER_READ_BIT;
            barriers[barrierCount].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barriers[barrierCount].image=renderImg;
            barriers[barrierCount].subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            barrierCount++;
            if(depthImg!=VK_NULL_HANDLE){
                barriers[barrierCount].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[barrierCount].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
                barriers[barrierCount].newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                barriers[barrierCount].srcAccessMask=0;
                barriers[barrierCount].dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                barriers[barrierCount].image=depthImg;
                barriers[barrierCount].subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,1};
                barrierCount++;
            }
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,0,0,nullptr,0,nullptr,barrierCount,barriers);

            VkRenderingAttachmentInfo ca{}; ca.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            ca.imageView=vkRenderFbo->GetColorAttachmentImageView(0); ca.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ca.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; ca.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
            ca.clearValue.color={{0.11f,0.11f,0.12f,1.0f}};

            VkRenderingAttachmentInfo da{};
            bool hasDepth=depthImg!=VK_NULL_HANDLE;
            if(hasDepth){
                da.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                da.imageView=vkRenderFbo->GetDepthAttachmentImageView();
                da.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                da.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
                da.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
                da.clearValue.depthStencil={1.0f,0};
            }
            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO,nullptr,0,{{0,0},{kPreviewSize,kPreviewSize}},1,0,1,&ca,hasDepth?&da:nullptr};
            vkCmdBeginRendering(cb,&ri);
            VkViewport vp{0,(float)kPreviewSize,(float)kPreviewSize,-(float)kPreviewSize,0,1}; vkCmdSetViewport(cb,0,1,&vp);
            VkRect2D sc{{0,0},{kPreviewSize,kPreviewSize}}; vkCmdSetScissor(cb,0,1,&sc);

            if(usePBR){
                vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_PbrVk.pipeline);
                VkDescriptorSet ssets[2]={s_PbrVk.set0,s_PbrVk.set1[fi]}; vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_PbrVk.pipelineLayout,0,2,ssets,0,nullptr);
                for(auto&m:mdl->GetMeshes()){if(!m->GetIndexCount())continue; vkCmdPushConstants(cb,s_PbrVk.pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(PBRPush),&pbr);
                    auto vB=std::dynamic_pointer_cast<VulkanVertexBuffer>(m->GetVertexBuffer());auto vI=std::dynamic_pointer_cast<VulkanIndexBuffer>(m->GetIndexBuffer());
                    if(vB&&vI){VkBuffer vbs[]={vB->GetVulkanBuffer()};VkDeviceSize off[]={0};vkCmdBindVertexBuffers(cb,0,1,vbs,off);vkCmdBindIndexBuffer(cb,vI->GetVulkanBuffer(),0,VK_INDEX_TYPE_UINT32);vkCmdDrawIndexed(cb,m->GetIndexCount(),1,0,0,0);}
                }
            }else{
                vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,vkPipe->GetVulkanPipeline());
                vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,vkPipe->GetVulkanPipelineLayout(),0,1,&s_ThumbnailVk.descriptorSet,0,nullptr);
                for(auto&m:mdl->GetMeshes()){if(!m->GetIndexCount())continue; vkCmdPushConstants(cb,vkPipe->GetVulkanPipelineLayout(),VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(PreviewPushConstants),&wm);
                    auto vB=std::dynamic_pointer_cast<VulkanVertexBuffer>(m->GetVertexBuffer());auto vI=std::dynamic_pointer_cast<VulkanIndexBuffer>(m->GetIndexBuffer());
                    if(vB&&vI){VkBuffer vbs[]={vB->GetVulkanBuffer()};VkDeviceSize off[]={0};vkCmdBindVertexBuffers(cb,0,1,vbs,off);vkCmdBindIndexBuffer(cb,vI->GetVulkanBuffer(),0,VK_INDEX_TYPE_UINT32);vkCmdDrawIndexed(cb,m->GetIndexCount(),1,0,0,0);}
                }
            }
            vkCmdEndRendering(cb);

            // If rendered to MSAA FBO, resolve to non-MSAA FBO for copy
            if(!usePBR){
                auto vkMSAA = vkRenderFbo;
                auto vkResolve = std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO);
                VkImage msaaImg=vkMSAA->GetColorAttachmentImage(0), resImg=vkResolve->GetColorAttachmentImage(0);

                VkImageMemoryBarrier pre[2]{};
                pre[0].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; pre[0].oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; pre[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                pre[0].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; pre[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                pre[0].image=msaaImg; pre[0].subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                pre[1].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; pre[1].oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; pre[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                pre[1].srcAccessMask=VK_ACCESS_SHADER_READ_BIT; pre[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                pre[1].image=resImg; pre[1].subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,pre);
                VkImageResolve res{}; res.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; res.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; res.extent={kPreviewSize,kPreviewSize,1};
                vkCmdResolveImage(cb,msaaImg,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,resImg,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&res);
                VkImageMemoryBarrier post[2]{};
                post[0].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; post[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; post[0].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                post[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; post[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                post[0].image=msaaImg; post[0].subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                post[1].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; post[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; post[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                post[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; post[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                post[1].image=resImg; post[1].subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,2,post);
            }
        }

        // Copy rendered result to destination texture + transition both back
        {
            auto vkSrcFbo=std::dynamic_pointer_cast<VulkanFramebuffer>(s_PreviewFBO);
            VkImage srcImg=vkSrcFbo->GetColorAttachmentImage(0);

            // src may be COLOR_ATTACHMENT (PBR) or TRANSFER_SRC (white-model after resolve)
            VkImageLayout srcLayout = usePBR ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            VkAccessFlags srcAccess = usePBR ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT;
            VkPipelineStageFlags srcStage = usePBR ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;

            if (usePBR) {
                VkImageMemoryBarrier b{};
                b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout=srcLayout; b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                b.srcAccessMask=srcAccess; b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                b.image=srcImg; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                vkCmdPipelineBarrier(cb,srcStage,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
                srcLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }

            VkImageCopy region{};
            region.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            region.extent={kPreviewSize,kPreviewSize,1};
            vkCmdCopyImage(cb,srcImg,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dstImg,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);

            VkImageMemoryBarrier post[2]{}; uint32_t n=0;
            // src: back to SHADER_READ_ONLY
            post[n].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            post[n].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            post[n].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            post[n].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            post[n].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            post[n].image=srcImg;
            post[n].subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; n++;
            // dst: TRANSFER_DST → SHADER_READ_ONLY
            post[n].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            post[n].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            post[n].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            post[n].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            post[n].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            post[n].image=dstImg;
            post[n].subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; n++;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,n,post);
        }

        s_ThumbCache[r.handle]=dstTex;
        s_ThumbQueue.erase(s_ThumbQueue.begin());
    }

    std::shared_ptr<Texture2D> AssetPreviewer::GetCachedThumbnail(UUID handle) {
        auto it=s_ThumbCache.find(handle);
        return it!=s_ThumbCache.end()?it->second:nullptr;
    }

    void AssetPreviewer::InvalidateThumbnail(UUID handle) {
        s_ThumbCache.erase(handle);
    }

}
