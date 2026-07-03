#include "ayapch.h"
#include "VulkanDepthPrePass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanShader.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Platform/Vulkan/VulkanStorageBuffer.hpp"
#include "Core/Application.hpp"
#include "Core/VFS.hpp"

namespace Ayaya {

    static constexpr uint32_t kMaxInstances = 65536;

    // Push constant struct shared between OnAttach (pipeline layout) and Execute (dispatch).
    // Must stay in sync with cull.comp GLSL push constant layout.
    struct DepthPrePassPC {
        glm::vec4 planes[6];           // 96
        uint32_t  instanceCount;       // 4
        uint32_t  lightModeMask;       // 4
        uint32_t  overrideInstanceID;  // 4
        uint32_t  _pad;               // 4
        glm::vec4 texelSize;          // 16
        float     exposureInverse;    // 4
    };

    VulkanDepthPrePass::~VulkanDepthPrePass() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();
        vkDeviceWaitIdle(device);
        if (m_CullPipeline)     vkDestroyPipeline(device, m_CullPipeline, nullptr);
        if (m_CullLayout)       vkDestroyPipelineLayout(device, m_CullLayout, nullptr);
        if (m_CullShader)       vkDestroyShaderModule(device, m_CullShader, nullptr);
        if (m_CullSet3Pool)     vkDestroyDescriptorPool(device, m_CullSet3Pool, nullptr);
        if (m_CullSet3Layout)   vkDestroyDescriptorSetLayout(device, m_CullSet3Layout, nullptr);
        if (m_CullDummyLayout)  vkDestroyDescriptorSetLayout(device, m_CullDummyLayout, nullptr);
    }

    void VulkanDepthPrePass::OnAttach() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();
        uint32_t fiCount = vkCtx->GetFramesInFlight();

        // ── Load cull.comp SPIR-V ──
        auto exePath = std::filesystem::current_path();
        std::string spvPath = (exePath / "assets/Editor/shaders/cache/vulkan/Deferred/cull.comp.spv").string();
        std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            AYAYA_CORE_ERROR("[DepthPrePass] Failed to open cull.comp.spv");
            return;
        }
        size_t sz = (size_t)file.tellg();
        std::vector<char> buf(sz);
        file.seekg(0); file.read(buf.data(), sz); file.close();
        VkShaderModuleCreateInfo sm{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        sm.codeSize = buf.size(); sm.pCode = reinterpret_cast<const uint32_t*>(buf.data());
        vkCreateShaderModule(device, &sm, nullptr, &m_CullShader);

        // ── Indirect draw buffer ──
        m_DrawIndirectBuffer = std::make_unique<VulkanStorageBuffer>(
            kMaxInstances * sizeof(VkDrawIndexedIndirectCommand),
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

        // ── Set 3: DrawIndirectBuffer descriptor ──
        VkDescriptorSetLayoutBinding set3Bind{};
        set3Bind.binding = 0; set3Bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        set3Bind.descriptorCount = 1; set3Bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo set3Info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        set3Info.bindingCount = 1; set3Info.pBindings = &set3Bind;
        vkCreateDescriptorSetLayout(device, &set3Info, nullptr, &m_CullSet3Layout);

        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets = 10; poolInfo.poolSizeCount = 1; poolInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_CullSet3Pool);

        m_CullSet3Descriptors.resize(fiCount);
        std::vector<VkDescriptorSetLayout> layouts(fiCount, m_CullSet3Layout);
        VkDescriptorSetAllocateInfo setAI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        setAI.descriptorPool = m_CullSet3Pool; setAI.descriptorSetCount = fiCount;
        setAI.pSetLayouts = layouts.data();
        vkAllocateDescriptorSets(device, &setAI, m_CullSet3Descriptors.data());
        for (uint32_t i = 0; i < fiCount; i++) {
            VkDescriptorBufferInfo bufInfo{ m_DrawIndirectBuffer->GetBuffer(i), 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = m_CullSet3Descriptors[i]; w.dstBinding = 0;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w.descriptorCount = 1; w.pBufferInfo = &bufInfo;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }

        // ── Dummy Set 0/1 layout ──
        VkDescriptorSetLayoutCreateInfo dummyInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        vkCreateDescriptorSetLayout(device, &dummyInfo, nullptr, &m_CullDummyLayout);

        // ── Compute pipeline ──
        if (m_GDRCtx && m_GDRCtx->Set2Layout != VK_NULL_HANDLE) {
            VkDescriptorSetLayout cullLayouts[] = { m_CullDummyLayout, m_CullDummyLayout,
                m_GDRCtx->Set2Layout, m_CullSet3Layout };
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DepthPrePassPC) };
            VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            plInfo.setLayoutCount = 4; plInfo.pSetLayouts = cullLayouts;
            plInfo.pushConstantRangeCount = 1; plInfo.pPushConstantRanges = &pcRange;
            vkCreatePipelineLayout(device, &plInfo, nullptr, &m_CullLayout);

            VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpInfo.stage.module = m_CullShader; cpInfo.stage.pName = "main";
            cpInfo.layout = m_CullLayout;
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_CullPipeline);
        }

        // ── Graphics pipeline: depth-only ──
        m_Shader = Shader::Create("Deferred/gbuffer_gdr.vert", "Deferred/depth_only.frag");
        if (m_Shader && m_GDRCtx) {
            // Reference FBO spec: depth-only, no color attachments
            FramebufferSpecification depthSpec;
            depthSpec.Width = 1280; depthSpec.Height = 720;
            depthSpec.Attachments.Attachments.push_back({FramebufferTextureFormat::R8});
            depthSpec.Attachments.Attachments.push_back({FramebufferTextureFormat::Depth});
            auto refFBO = Framebuffer::Create(depthSpec);

            PipelineSpecification pipeSpec;
            pipeSpec.Shader = m_Shader;
            pipeSpec.TargetFramebuffer = refFBO;
            pipeSpec.Layout = {}; // SSBO vertex pulling
            pipeSpec.DepthTest = true; pipeSpec.DepthWrite = true;
            pipeSpec.DepthOperator = DepthCompareOperator::Less;
            pipeSpec.Blend = false;
            pipeSpec.BackfaceCulling = CullMode::Back;
            pipeSpec.ColorWrite = false;
            pipeSpec.NoTextureDescriptors = false;
            VulkanPipeline::s_ExtraSetLayouts = { m_GDRCtx->Set2Layout };
            m_Pipeline = Pipeline::Create(pipeSpec);
            VulkanPipeline::s_ExtraSetLayouts.clear();
        }
    }

    void VulkanDepthPrePass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        if (!m_GDRCtx || !m_Pipeline) return;
        uint32_t instanceCount = std::min(m_GDRCtx->InstanceCount, kMaxInstances);
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
        uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();

        // ── Compute culling (outside render pass) — only when instances exist ──
        if (instanceCount > 0) {
            VkMemoryBarrier hostBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            hostBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            hostBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDEX_READ_BIT;
            vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                    | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
                1, &hostBarrier, 0, nullptr, 0, nullptr);

            vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline);
            m_GDRCtx->BindSet2(vkCmd, m_CullLayout, VK_PIPELINE_BIND_POINT_COMPUTE, frameIdx);
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                m_CullLayout, 3, 1, &m_CullSet3Descriptors[frameIdx], 0, nullptr);

            DepthPrePassPC fpc{};
            fpc.lightModeMask = 1;
            fpc.overrideInstanceID = 0xFFFFFFFF;
            glm::mat4 vp = context.ProjectionMatrix * context.ViewMatrix;
            glm::mat4 vpT = glm::transpose(vp);
            glm::vec4 rows[4] = { vpT[0], vpT[1], vpT[2], vpT[3] };
            fpc.planes[0] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[0]), rows[3].w + rows[0].w));
            fpc.planes[1] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[0]), rows[3].w - rows[0].w));
            fpc.planes[2] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[1]), rows[3].w + rows[1].w));
            fpc.planes[3] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[1]), rows[3].w - rows[1].w));
            fpc.planes[4] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[2]), rows[3].w + rows[2].w));
            fpc.planes[5] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[2]), rows[3].w - rows[2].w));
            fpc.instanceCount = instanceCount;
            vkCmdPushConstants(vkCmd, m_CullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fpc), &fpc);
            uint32_t groupCount = (instanceCount + 63) / 64;
            vkCmdDispatch(vkCmd, groupCount, 1, 1);

            VkBufferMemoryBarrier indirectBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            indirectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            indirectBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            indirectBarrier.buffer = m_DrawIndirectBuffer->GetBuffer(frameIdx);
            indirectBarrier.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, 1, &indirectBarrier, 0, nullptr);
        }

        // ── Always clear SceneDepth (prevents ghost silhouettes when objects hidden) ──
        auto fbo = context.GetFramebuffer("SceneDepth");
        if (!fbo) return;
        cmd.BeginRenderPass(fbo, /*clearColor=*/false, /*clearDepth=*/true);

        if (instanceCount > 0) {
            auto& pool = vkCtx->GetGeometryPool();
            vkCmdBindIndexBuffer(vkCmd, pool.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
            cmd.BindPipeline(m_Pipeline);
            auto gdrPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_Pipeline);
            if (gdrPipe && m_GDRCtx) {
                vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    gdrPipe->GetVulkanPipelineLayout(),
                    2, 1, &m_GDRCtx->Set2Descriptors[frameIdx], 0, nullptr);
            }
            vkCmdDrawIndexedIndirect(vkCmd, m_DrawIndirectBuffer->GetBuffer(frameIdx), 0,
                instanceCount, sizeof(VkDrawIndexedIndirectCommand));
        }
        cmd.EndRenderPass();
    }

    void VulkanDepthPrePass::DeclareResources(RGBuilder& builder, uint32_t vpW, uint32_t vpH) {
        FramebufferSpecification depthSpec;
        depthSpec.Width = vpW; depthSpec.Height = vpH;
        depthSpec.Attachments.Attachments.push_back({FramebufferTextureFormat::R8});   // dummy: fragment shader needs a color target
        depthSpec.Attachments.Attachments.push_back({FramebufferTextureFormat::Depth});
        builder.WriteTexture("SceneDepth", depthSpec);
    }

} // namespace Ayaya
