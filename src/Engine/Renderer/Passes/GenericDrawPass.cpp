#include "ayapch.h"
#include "GenericDrawPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/PassRegistry.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/Frustum.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"

namespace Ayaya {

    static constexpr uint32_t kMaxInstances = 65536;

    GenericDrawPass::~GenericDrawPass() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();

        if (m_CullPipeline)  vkDestroyPipeline(device, m_CullPipeline, nullptr);
        if (m_CullLayout)    vkDestroyPipelineLayout(device, m_CullLayout, nullptr);
        if (m_CullShader)    vkDestroyShaderModule(device, m_CullShader, nullptr);
        if (m_CullSet3Pool)  vkDestroyDescriptorPool(device, m_CullSet3Pool, nullptr);
        if (m_CullSet3Layout) vkDestroyDescriptorSetLayout(device, m_CullSet3Layout, nullptr);
    }

    void GenericDrawPass::OnAttach() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();
        uint32_t fiCount = vkCtx->GetFramesInFlight();

        // ── Load cull.comp SPIR-V (shared compute shader) ──
        auto exePath = std::filesystem::current_path();
        std::string spvPath = (exePath / "assets/Editor/shaders/cache/vulkan/Deferred/cull.comp.spv").string();
        std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            AYAYA_CORE_ERROR("[GenericDraw] Failed to open cull.comp.spv at {}", spvPath);
            return;
        }
        size_t sz = (size_t)file.tellg();
        std::vector<char> buf(sz);
        file.seekg(0); file.read(buf.data(), sz); file.close();
        VkShaderModuleCreateInfo sm{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        sm.codeSize = buf.size(); sm.pCode = reinterpret_cast<const uint32_t*>(buf.data());
        vkCreateShaderModule(device, &sm, nullptr, &m_CullShader);

        // ── Indirect draw buffer (triple-buffered) ──
        m_DrawIndirectBuffer = std::make_unique<VulkanStorageBuffer>(
            kMaxInstances * sizeof(VkDrawIndexedIndirectCommand),
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

        // ── Set 3: DrawIndirectBuffer descriptor ──
        VkDescriptorSetLayoutBinding set3Bind{};
        set3Bind.binding = 0;
        set3Bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        set3Bind.descriptorCount = 1;
        set3Bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
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
            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = m_DrawIndirectBuffer->GetBuffer(i);
            bufInfo.range = VK_WHOLE_SIZE;
            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = m_CullSet3Descriptors[i]; w.dstBinding = 0;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w.descriptorCount = 1; w.pBufferInfo = &bufInfo;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }

        // ── Dummy Set 0 layout (cull.comp uses GDR set=2, not per-pipeline set=0) ──
        VkDescriptorSetLayout dummyLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayoutCreateInfo dummyInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        vkCreateDescriptorSetLayout(device, &dummyInfo, nullptr, &dummyLayout);

        // ── Compute pipeline layout ──
        VkDescriptorSetLayout cullLayouts[] = { dummyLayout, dummyLayout,
            m_GDRCtx ? m_GDRCtx->Set2Layout : VK_NULL_HANDLE, m_CullSet3Layout };
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 112 };
        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 4; plInfo.pSetLayouts = cullLayouts;
        plInfo.pushConstantRangeCount = 1; plInfo.pPushConstantRanges = &pcRange;
        vkCreatePipelineLayout(device, &plInfo, nullptr, &m_CullLayout);

        VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpInfo.stage.module = m_CullShader;
        cpInfo.stage.pName = "main";
        cpInfo.layout = m_CullLayout;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_CullPipeline);

        vkDestroyDescriptorSetLayout(device, dummyLayout, nullptr);
    }

    void GenericDrawPass::DeclareResources(RGBuilder& builder, uint32_t w, uint32_t h,
                                            const PassBakedParams& params) {
        // Explicit writes declared by PipelineBuilder from Lua
    }

    std::shared_ptr<Shader> GenericDrawPass::LoadShader(
        const std::string& vertPath, const std::string& fragPath) {
        return Shader::Create(vertPath, fragPath);
    }

    std::shared_ptr<Pipeline> GenericDrawPass::GetOrCreatePipeline(const PipelineKey& key) {
        auto it = m_PipelineCache.find(key);
        if (it != m_PipelineCache.end()) return it->second;

        auto shader = LoadShader(key.shader + ".vert", key.shader + ".frag");
        if (!shader) {
            AYAYA_CORE_ERROR("[GenericDraw] Failed to load shader: {}", key.shader);
            return nullptr;
        }

        PipelineSpecification spec;
        spec.Shader = shader;
        spec.DepthTest = key.depthTest;
        spec.DepthWrite = key.depthWrite;
        spec.BackfaceCulling = (key.cullMode == 0) ? CullMode::None :
                               (key.cullMode == 2) ? CullMode::Back :
                               CullMode::Front;
        spec.Blend = (key.blendMode != 0);
        spec.BlendMode = (key.blendMode == 1) ? BlendModeType::Additive :
                         (key.blendMode == 2) ? BlendModeType::Alpha :
                         BlendModeType::None;
        spec.NoTextureDescriptors = false;

        auto pipeline = Pipeline::Create(spec);
        if (pipeline) m_PipelineCache[key] = pipeline;
        return pipeline;
    }

    void GenericDrawPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        if (!m_GDRCtx || m_GDRCtx->InstanceCount == 0) return;

        // Read Lua-baked params (use node name from Lua, not pass type name)
        std::string prefix = m_NodeName.empty() ? m_PassName : m_NodeName;
        uint32_t lightModeMask = (uint32_t)context.Get<int>(prefix + ".LightModeMask", 1);
        bool depthTest  = context.Get<int>(prefix + ".DepthTest", 1) != 0;
        bool depthWrite = context.Get<int>(prefix + ".DepthWrite", 1) != 0;
        int cullMode    = context.Get<int>(prefix + ".CullMode", 2);
        int blendMode   = context.Get<int>(prefix + ".BlendMode", 0);
        std::string shaderPath = context.Get<std::string>(prefix + ".Shader", "");
        std::string targetName = context.Get<std::string>(prefix + ".Target", "");

        if (shaderPath.empty() || targetName.empty()) {
            AYAYA_CORE_WARN("[GenericDraw] Missing Shader or Target param");
            return;
        }

        // Get or create graphics pipeline
        PipelineKey key{ shaderPath, depthTest, depthWrite, cullMode, blendMode };
        auto pipeline = GetOrCreatePipeline(key);
        if (!pipeline) return;

        // Get target FBO
        auto fbo = context.GetFramebuffer(targetName);
        if (!fbo) {
            AYAYA_CORE_WARN("[GenericDraw] Target FBO '{}' not found", targetName);
            return;
        }

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
        uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();

        // ── Compute culling (outside render pass) ──
        VkMemoryBarrier hostBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        hostBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                                 | VK_ACCESS_INDEX_READ_BIT;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
            1, &hostBarrier, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline);
        m_GDRCtx->BindSet2(vkCmd, m_CullLayout, VK_PIPELINE_BIND_POINT_COMPUTE, frameIdx);
        vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_CullLayout, 3, 1, &m_CullSet3Descriptors[frameIdx], 0, nullptr);

        struct FrustumPush { glm::vec4 planes[6]; uint32_t count; uint32_t lightModeMask; uint32_t _pad[2]; } fpc;
        glm::mat4 vp = context.ProjectionMatrix * context.ViewMatrix;
        glm::mat4 vpT = glm::transpose(vp);
        for (int i = 0; i < 6; i++) {
            float len = glm::length(glm::vec3(vpT[i]));
            fpc.planes[i] = vpT[i] / len;
        }
        fpc.count = m_GDRCtx->InstanceCount;
        fpc.lightModeMask = lightModeMask;
        vkCmdPushConstants(vkCmd, m_CullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fpc), &fpc);

        uint32_t groupCount = (m_GDRCtx->InstanceCount + 63) / 64;
        vkCmdDispatch(vkCmd, groupCount, 1, 1);

        VkBufferMemoryBarrier indirectBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        indirectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        indirectBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        indirectBarrier.buffer = m_DrawIndirectBuffer->GetBuffer(frameIdx);
        indirectBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
            1, nullptr, 1, &indirectBarrier, 0, nullptr);

        // ── Graphics pass ──
        cmd.BeginRenderPass(fbo, true);
        cmd.BindPipeline(pipeline);

        uint32_t instanceCount = std::min(m_GDRCtx->InstanceCount, kMaxInstances);
        // Issue indirect draw using the global geometry pool
        auto& pool = vkCtx->GetGeometryPool();
        VkDeviceSize vbOffsets[] = { 0 };
        VkBuffer vb = pool.GetBuffer();
        vkCmdBindVertexBuffers(vkCmd, 0, 1, &vb, vbOffsets);
        vkCmdBindIndexBuffer(vkCmd, pool.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(vkCmd, m_DrawIndirectBuffer->GetBuffer(frameIdx), 0,
            instanceCount, sizeof(VkDrawIndexedIndirectCommand));

        cmd.EndRenderPass();
    }

} // namespace Ayaya
