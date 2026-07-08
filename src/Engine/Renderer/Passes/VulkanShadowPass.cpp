#include "ayapch.h"
#include "VulkanShadowPass.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Material.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Core/Application.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>

namespace Ayaya {

    VulkanShadowPass::VulkanShadowPass() { m_PassName = "Shadow Map Pass"; }

    VulkanShadowPass::~VulkanShadowPass() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!context) return;
        VkDevice device = context->GetDevice();

        // Atomic path resources
        if (m_ShadowSet3Layout) vkDestroyDescriptorSetLayout(device, m_ShadowSet3Layout, nullptr);
        if (m_ShadowSet3Pool)   vkDestroyDescriptorPool(device, m_ShadowSet3Pool, nullptr);
        if (m_CullDummyLayout)  vkDestroyDescriptorSetLayout(device, m_CullDummyLayout, nullptr);
        if (m_CullLayout)       vkDestroyPipelineLayout(device, m_CullLayout, nullptr);
        if (m_CullPipeline)     vkDestroyPipeline(device, m_CullPipeline, nullptr);
        if (m_CullShaderModule) vkDestroyShaderModule(device, m_CullShaderModule, nullptr);

        // Fixed-slot path resources
        if (m_ShadowSet3LayoutFixed) vkDestroyDescriptorSetLayout(device, m_ShadowSet3LayoutFixed, nullptr);
        if (m_ShadowSet3PoolFixed)   vkDestroyDescriptorPool(device, m_ShadowSet3PoolFixed, nullptr);
        if (m_CullLayoutFixed)       vkDestroyPipelineLayout(device, m_CullLayoutFixed, nullptr);
        if (m_CullPipelineFixed)     vkDestroyPipeline(device, m_CullPipelineFixed, nullptr);
        if (m_CullShaderModuleFixed) vkDestroyShaderModule(device, m_CullShaderModuleFixed, nullptr);

        m_ShadowSet3Descriptors.clear();
        m_ShadowSet3DescriptorsFixed.clear();
    }

    void VulkanShadowPass::InitGDR(VkDevice device, uint32_t framesInFlight) {
        // ── Read capabilities for feature-driven routing ──
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (vkCtx) {
            m_HasDrawIndirectCount = vkCtx->GetCapabilities().HasDrawIndirectCount;
        }

        auto exePath = std::filesystem::current_path();

        // ═══════════════════════════════════════════════════════════════
        // Shared resources (needed by both paths)
        // ═══════════════════════════════════════════════════════════════

        // Indirect buffers (opaque + masked — both paths need these)
        m_OpaqueIndirectBuffer = std::make_unique<VulkanStorageBuffer>(
            kGDRMaxInstances * sizeof(VkDrawIndexedIndirectCommand),
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        m_MaskedIndirectBuffer = std::make_unique<VulkanStorageBuffer>(
            kGDRMaxInstances * sizeof(VkDrawIndexedIndirectCommand),
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

        // Dummy descriptor set layout for unused sets 0 and 1 (MoltenVK compat)
        {
            VkDescriptorSetLayoutCreateInfo dummyCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            dummyCI.bindingCount = 0;
            vkCreateDescriptorSetLayout(device, &dummyCI, nullptr, &m_CullDummyLayout);
        }

        // ═══════════════════════════════════════════════════════════════
        // Path-specific resources: exactly one branch executes
        // ═══════════════════════════════════════════════════════════════
        if (m_HasDrawIndirectCount) {
            // ── Atomic path: desktop GPUs with drawIndirectCount ──
            // Count buffers (atomic-only)
            m_OpaqueCountBuffer = std::make_unique<VulkanStorageBuffer>(
                sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
            m_MaskedCountBuffer = std::make_unique<VulkanStorageBuffer>(
                sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

            // Set=3 descriptor layout: 4 SSBO bindings
            VkDescriptorSetLayoutBinding set3Bindings[4] = {};
            set3Bindings[0].binding = 0; set3Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            set3Bindings[0].descriptorCount = 1; set3Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            set3Bindings[1].binding = 1; set3Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            set3Bindings[1].descriptorCount = 1; set3Bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            set3Bindings[2].binding = 2; set3Bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            set3Bindings[2].descriptorCount = 1; set3Bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            set3Bindings[3].binding = 3; set3Bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            set3Bindings[3].descriptorCount = 1; set3Bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutCreateInfo set3LCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            set3LCI.bindingCount = 4; set3LCI.pBindings = set3Bindings;
            vkCreateDescriptorSetLayout(device, &set3LCI, nullptr, &m_ShadowSet3Layout);

            VkDescriptorPoolSize set3PS{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, framesInFlight * 4 };
            VkDescriptorPoolCreateInfo set3PCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            set3PCI.maxSets = framesInFlight; set3PCI.poolSizeCount = 1; set3PCI.pPoolSizes = &set3PS;
            vkCreateDescriptorPool(device, &set3PCI, nullptr, &m_ShadowSet3Pool);

            m_ShadowSet3Descriptors.resize(framesInFlight);
            std::vector<VkDescriptorSetLayout> set3Layouts(framesInFlight, m_ShadowSet3Layout);
            VkDescriptorSetAllocateInfo set3AI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            set3AI.descriptorPool = m_ShadowSet3Pool; set3AI.descriptorSetCount = framesInFlight;
            set3AI.pSetLayouts = set3Layouts.data();
            vkAllocateDescriptorSets(device, &set3AI, m_ShadowSet3Descriptors.data());

            for (uint32_t i = 0; i < framesInFlight; i++) {
                VkDescriptorBufferInfo opaqueCmdI{}, opaqueCntI{}, maskedCmdI{}, maskedCntI{};
                opaqueCmdI.buffer = m_OpaqueIndirectBuffer->GetBuffer(i); opaqueCmdI.range = VK_WHOLE_SIZE;
                opaqueCntI.buffer = m_OpaqueCountBuffer->GetBuffer(i);   opaqueCntI.range = VK_WHOLE_SIZE;
                maskedCmdI.buffer = m_MaskedIndirectBuffer->GetBuffer(i); maskedCmdI.range = VK_WHOLE_SIZE;
                maskedCntI.buffer = m_MaskedCountBuffer->GetBuffer(i);   maskedCntI.range = VK_WHOLE_SIZE;

                VkWriteDescriptorSet w[4]{};
                w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[0].dstSet = m_ShadowSet3Descriptors[i]; w[0].dstBinding = 0;
                w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[0].descriptorCount = 1; w[0].pBufferInfo = &opaqueCmdI;
                w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[1].dstSet = m_ShadowSet3Descriptors[i]; w[1].dstBinding = 1;
                w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[1].descriptorCount = 1; w[1].pBufferInfo = &opaqueCntI;
                w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[2].dstSet = m_ShadowSet3Descriptors[i]; w[2].dstBinding = 2;
                w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[2].descriptorCount = 1; w[2].pBufferInfo = &maskedCmdI;
                w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[3].dstSet = m_ShadowSet3Descriptors[i]; w[3].dstBinding = 3;
                w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[3].descriptorCount = 1; w[3].pBufferInfo = &maskedCntI;
                vkUpdateDescriptorSets(device, 4, w, 0, nullptr);
            }

            // Load atomic-compacted compute shader
            std::string spvPath = (exePath / "assets/Editor/shaders/cache/vulkan/Shadow/cull_shadow_atomic.comp.spv").string();
            std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
            if (file.is_open()) {
                size_t fileSize = (size_t)file.tellg();
                std::vector<char> buffer(fileSize);
                file.seekg(0); file.read(buffer.data(), fileSize); file.close();
                VkShaderModuleCreateInfo smCI{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                smCI.codeSize = buffer.size();
                smCI.pCode = reinterpret_cast<const uint32_t*>(buffer.data());
                vkCreateShaderModule(device, &smCI, nullptr, &m_CullShaderModule);
            }

            // Compute pipeline layout + pipeline
            {
                VkDescriptorSetLayout compSetLayouts[] = {
                    m_CullDummyLayout, m_CullDummyLayout,
                    m_GDRCtx->Set2Layout, m_ShadowSet3Layout
                };
                VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 112 };
                VkPipelineLayoutCreateInfo plCI{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
                plCI.setLayoutCount = 4; plCI.pSetLayouts = compSetLayouts;
                plCI.pushConstantRangeCount = 1; plCI.pPushConstantRanges = &pcRange;
                vkCreatePipelineLayout(device, &plCI, nullptr, &m_CullLayout);
            }

            if (m_CullShaderModule) {
                VkComputePipelineCreateInfo cpCI{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                cpCI.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                cpCI.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                cpCI.stage.module = m_CullShaderModule;
                cpCI.stage.pName = "main";
                cpCI.layout = m_CullLayout;
                vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpCI, nullptr, &m_CullPipeline);
            }

            AYAYA_CORE_INFO("Shadow Pass GDR: atomic path (drawIndirectCount=true)");
        } else {
            // ── Fixed-slot path: MoltenVK/M1 fallback ──
            // (no count buffers — culled instances get instanceCount=0)

            // Set=3 descriptor layout: 2 SSBO bindings, contiguous (binding 0=opaque, binding 1=masked)
            VkDescriptorSetLayoutBinding set3Bindings[2] = {};
            set3Bindings[0].binding = 0; set3Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            set3Bindings[0].descriptorCount = 1; set3Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            set3Bindings[1].binding = 1; set3Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            set3Bindings[1].descriptorCount = 1; set3Bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutCreateInfo set3LCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            set3LCI.bindingCount = 2; set3LCI.pBindings = set3Bindings;
            vkCreateDescriptorSetLayout(device, &set3LCI, nullptr, &m_ShadowSet3LayoutFixed);

            VkDescriptorPoolSize set3PS{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, framesInFlight * 2 };
            VkDescriptorPoolCreateInfo set3PCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            set3PCI.maxSets = framesInFlight; set3PCI.poolSizeCount = 1; set3PCI.pPoolSizes = &set3PS;
            vkCreateDescriptorPool(device, &set3PCI, nullptr, &m_ShadowSet3PoolFixed);

            m_ShadowSet3DescriptorsFixed.resize(framesInFlight);
            std::vector<VkDescriptorSetLayout> set3Layouts(framesInFlight, m_ShadowSet3LayoutFixed);
            VkDescriptorSetAllocateInfo set3AI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            set3AI.descriptorPool = m_ShadowSet3PoolFixed; set3AI.descriptorSetCount = framesInFlight;
            set3AI.pSetLayouts = set3Layouts.data();
            vkAllocateDescriptorSets(device, &set3AI, m_ShadowSet3DescriptorsFixed.data());

            for (uint32_t i = 0; i < framesInFlight; i++) {
                VkDescriptorBufferInfo opaqueCmdI{}, maskedCmdI{};
                opaqueCmdI.buffer = m_OpaqueIndirectBuffer->GetBuffer(i); opaqueCmdI.range = VK_WHOLE_SIZE;
                maskedCmdI.buffer = m_MaskedIndirectBuffer->GetBuffer(i); maskedCmdI.range = VK_WHOLE_SIZE;

                VkWriteDescriptorSet w[2]{};
                w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[0].dstSet = m_ShadowSet3DescriptorsFixed[i]; w[0].dstBinding = 0;
                w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[0].descriptorCount = 1; w[0].pBufferInfo = &opaqueCmdI;
                w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[1].dstSet = m_ShadowSet3DescriptorsFixed[i]; w[1].dstBinding = 1;
                w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[1].descriptorCount = 1; w[1].pBufferInfo = &maskedCmdI;
                vkUpdateDescriptorSets(device, 2, w, 0, nullptr);
            }

            // Load fixed-slot compute shader
            std::string spvPath = (exePath / "assets/Editor/shaders/cache/vulkan/Shadow/cull_shadow_fixed.comp.spv").string();
            std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
            if (file.is_open()) {
                size_t fileSize = (size_t)file.tellg();
                std::vector<char> buffer(fileSize);
                file.seekg(0); file.read(buffer.data(), fileSize); file.close();
                VkShaderModuleCreateInfo smCI{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                smCI.codeSize = buffer.size();
                smCI.pCode = reinterpret_cast<const uint32_t*>(buffer.data());
                vkCreateShaderModule(device, &smCI, nullptr, &m_CullShaderModuleFixed);
            }

            // Compute pipeline layout + pipeline
            {
                VkDescriptorSetLayout compSetLayouts[] = {
                    m_CullDummyLayout, m_CullDummyLayout,
                    m_GDRCtx->Set2Layout, m_ShadowSet3LayoutFixed
                };
                VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 112 };
                VkPipelineLayoutCreateInfo plCI{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
                plCI.setLayoutCount = 4; plCI.pSetLayouts = compSetLayouts;
                plCI.pushConstantRangeCount = 1; plCI.pPushConstantRanges = &pcRange;
                vkCreatePipelineLayout(device, &plCI, nullptr, &m_CullLayoutFixed);
            }

            if (m_CullShaderModuleFixed) {
                VkComputePipelineCreateInfo cpCI{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                cpCI.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                cpCI.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                cpCI.stage.module = m_CullShaderModuleFixed;
                cpCI.stage.pName = "main";
                cpCI.layout = m_CullLayoutFixed;
                vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpCI, nullptr, &m_CullPipelineFixed);
            }

            AYAYA_CORE_INFO("Shadow Pass GDR: fixed-slot path (drawIndirectCount=false)");
        }

        // ═══════════════════════════════════════════════════════════════
        // Shared graphics pipelines (identical for both paths)
        // ═══════════════════════════════════════════════════════════════

        // GDR opaque graphics pipeline (VS-only, depth-only)
        m_GDR_OpaqueShader = Shader::Create("Shadow/shadow_gdr_opaque.vert", "Shadow/shadow_map.frag");
        PipelineSpecification opaqueSpec;
        opaqueSpec.Shader = m_GDR_OpaqueShader;
        opaqueSpec.TargetFramebuffer = m_ShadowRefFBO;
        opaqueSpec.Layout = {};  // SSBO vertex pulling — no VBO inputs
        opaqueSpec.Topology = PrimitiveTopology::Triangles;  // TRIANGLE_LIST
        opaqueSpec.DepthTest = true; opaqueSpec.DepthWrite = true;
        opaqueSpec.BackfaceCulling = CullMode::Back;
        opaqueSpec.NoFragmentShader = true;  // VS-only — hardware Early-Z
        opaqueSpec.DepthBiasEnable = true;
        opaqueSpec.DepthBiasConstantFactor = 0.8f;
        opaqueSpec.DepthBiasSlopeFactor = 0.5f;
        opaqueSpec.DepthBiasClamp = 0.001f;
        VulkanPipeline::s_ExtraSetLayouts = { m_GDRCtx->Set2Layout };
        m_GDR_OpaquePipeline = Pipeline::Create(opaqueSpec);
        VulkanPipeline::s_ExtraSetLayouts.clear();

        // GDR masked graphics pipeline (VS+FS, alpha-test discard)
        m_GDR_MaskedShader = Shader::Create("Shadow/shadow_gdr_masked.vert", "Shadow/shadow_gdr_masked.frag");
        PipelineSpecification maskedSpec;
        maskedSpec.Shader = m_GDR_MaskedShader;
        maskedSpec.TargetFramebuffer = m_ShadowRefFBO;
        maskedSpec.Layout = {};  // SSBO vertex pulling
        maskedSpec.Topology = PrimitiveTopology::Triangles;  // TRIANGLE_LIST
        maskedSpec.DepthTest = true; maskedSpec.DepthWrite = true;
        maskedSpec.BackfaceCulling = CullMode::None;  // double-sided foliage
        maskedSpec.UseBindlessTextures = true;  // needs set=1 for bindless array
        maskedSpec.DepthBiasEnable = true;
        maskedSpec.DepthBiasConstantFactor = 0.8f;
        maskedSpec.DepthBiasSlopeFactor = 0.5f;
        maskedSpec.DepthBiasClamp = 0.001f;
        VulkanPipeline::s_ExtraSetLayouts = { m_GDRCtx->Set2Layout };
        m_GDR_MaskedPipeline = Pipeline::Create(maskedSpec);
        VulkanPipeline::s_ExtraSetLayouts.clear();
    }

    void VulkanShadowPass::OnAttach() {
        // ── Legacy CPU path shader ──
        m_ShadowShader = Shader::Create("Shadow/shadow_map.vert", "Shadow/shadow_map.frag");
        m_PipeSpec.Shader = m_ShadowShader;
        m_PipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal"   },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent"  },
        };
        m_PipeSpec.DepthTest = true;
        m_PipeSpec.DepthWrite = true;
        m_PipeSpec.BackfaceCulling = CullMode::Back;
        m_PipeSpec.DepthBiasEnable = true;
        m_PipeSpec.DepthBiasConstantFactor = 0.8f;
        m_PipeSpec.DepthBiasSlopeFactor = 0.5f;
        m_PipeSpec.DepthBiasClamp = 0.001f;

        // ── GDR shadow reference FBO (4096×4096 depth-only, same as DeclareResources) ──
        FramebufferSpecification refSpec;
        refSpec.Width = 4096; refSpec.Height = 4096; refSpec.Samples = 1;
        refSpec.Attachments = { FramebufferTextureFormat::Depth };
        refSpec.IsShadowMap = true;
        m_ShadowRefFBO = Framebuffer::Create(refSpec);

        // ── Init GDR path if context is available ──
        if (m_GDRCtx) {
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            if (vkCtx) {
                m_GDR_Set2Layout = m_GDRCtx->Set2Layout;
                InitGDR(vkCtx->GetDevice(), vkCtx->GetFramesInFlight());
            } else {
                m_UseGDR = false;
            }
        } else {
            m_UseGDR = false;
        }
    }

    void VulkanShadowPass::DeclareResources(RGBuilder& builder) {
        FramebufferSpecification spec;
        spec.Width  = 4096;
        spec.Height = 4096;
        spec.Samples = 1;
        spec.Attachments = { FramebufferTextureFormat::Depth };
        spec.IsShadowMap = true;
        builder.WriteTexture("ShadowMap", spec);
    }

    void VulkanShadowPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        // Check for active directional lights before any GPU work.
        // If all lights are hidden via visibility toggle, skip the entire shadow pass.
        auto lightView = context.ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();
        bool hasActiveLight = false;
        for (auto entityID : lightView) {
            Entity entity{entityID, context.ActiveScene.get()};
            if (!entity.IsActiveInHierarchy()) continue;
            hasActiveLight = true;
            break;
        }
        if (!hasActiveLight) {
            context.Set("ShadowMap_Output", std::shared_ptr<Framebuffer>(nullptr));
            context.Set("LightSpaceMatrix", glm::mat4(1.0f));
            return;
        }

        if (m_UseGDR && m_GDRCtx && m_GDRCtx->InstanceCount > 0)
            ExecuteGDR(context, cmd);
        else
            ExecuteCPU(context, cmd);
    }

    // ═══════════════════════════════════════════════════════════════
    // GPU-Driven Rendering path (router)
    // ═══════════════════════════════════════════════════════════════
    void VulkanShadowPass::ExecuteGDR(RenderContext& context, RenderCommandBuffer& cmd) {
        auto shadowFBO = context.GetFramebuffer("ShadowMap");
        if (!shadowFBO) return;

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;

        uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();

        // ── Compute light-space matrix (respect visibility toggle) ──
        auto lightView = context.ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();
        glm::vec3 lightDir(0.0f);
        bool hasActiveLight = false;
        for (auto entityID : lightView) {
            Entity entity{entityID, context.ActiveScene.get()};
            if (!entity.IsActiveInHierarchy()) continue;
            auto& tc = lightView.get<TransformComponent>(entityID);
            lightDir = glm::normalize(glm::vec3(tc.GetTransform() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            hasActiveLight = true;
            break;
        }

        glm::mat4 lightSpaceMatrix(1.0f);
        if (hasActiveLight) {
            glm::vec3 lightPos = -lightDir * 20.0f;
            glm::mat4 lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 50.0f);
            glm::mat4 depthCorrection = glm::mat4(1.0f);
            depthCorrection[2][2] = 0.5f;
            depthCorrection[3][2] = 0.5f;
            lightProjection = depthCorrection * lightProjection;
            glm::mat4 lightViewMatrix = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            lightSpaceMatrix = lightProjection * lightViewMatrix;
        }

        if (m_HasDrawIndirectCount)
            ExecuteGDR_Atomic(context, cmd, vkCtx.get(), frameIdx, lightSpaceMatrix);
        else
            ExecuteGDR_Fixed(context, cmd, vkCtx.get(), frameIdx, lightSpaceMatrix);
    }

    // ═══════════════════════════════════════════════════════════════
    // Atomic path: desktop GPUs with drawIndirectCount
    // Uses atomic-compacted binning + vkCmdDrawIndexedIndirectCount
    // ═══════════════════════════════════════════════════════════════
    void VulkanShadowPass::ExecuteGDR_Atomic(RenderContext& context, RenderCommandBuffer& cmd,
                                              VulkanContext* vkCtx, uint32_t frameIdx,
                                              const glm::mat4& lightSpaceMatrix) {
        auto shadowFBO = context.GetFramebuffer("ShadowMap");
        if (!shadowFBO) return;

        auto lightView = context.ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();
        bool hasLight = lightView.begin() != lightView.end();

        VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
        auto& pool = vkCtx->GetGeometryPool();

        // ── Zero count buffers via vkCmdFillBuffer ──
        vkCmdFillBuffer(vkCmd, m_OpaqueCountBuffer->GetBuffer(frameIdx), 0, sizeof(uint32_t), 0u);
        vkCmdFillBuffer(vkCmd, m_MaskedCountBuffer->GetBuffer(frameIdx), 0, sizeof(uint32_t), 0u);

        // Transfer → Compute barrier for count buffers
        VkBufferMemoryBarrier fillBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        fillBarrier.buffer = m_OpaqueCountBuffer->GetBuffer(frameIdx);
        fillBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 1, &fillBarrier, 0, nullptr);

        fillBarrier.buffer = m_MaskedCountBuffer->GetBuffer(frameIdx);
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 1, &fillBarrier, 0, nullptr);

        // ── Host → Device barrier ──
        VkMemoryBarrier hostBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        hostBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
            1, &hostBarrier, 0, nullptr, 0, nullptr);

        // ── Compute culling dispatch (atomic variant) ──
        vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline);
        m_GDRCtx->BindSet2(vkCmd, m_CullLayout, VK_PIPELINE_BIND_POINT_COMPUTE, frameIdx);
        vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_CullLayout, 3, 1, &m_ShadowSet3Descriptors[frameIdx], 0, nullptr);

        struct FrustumPush { glm::vec4 planes[6]; uint32_t count; uint32_t lightModeMask; uint32_t overrideInstanceID; uint32_t _pad; } fpc;
        fpc.lightModeMask = (uint32_t)context.Get<int>("Shadow.LightMode", 2); // default ShadowCaster
        fpc.overrideInstanceID = 0xFFFFFFFF;
        if (hasLight) {
            glm::mat4 vpT = glm::transpose(lightSpaceMatrix);
            glm::vec4 rows[4] = { vpT[0], vpT[1], vpT[2], vpT[3] };
            fpc.planes[0] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[0]), rows[3].w + rows[0].w));
            fpc.planes[1] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[0]), rows[3].w - rows[0].w));
            fpc.planes[2] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[1]), rows[3].w + rows[1].w));
            fpc.planes[3] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[1]), rows[3].w - rows[1].w));
            fpc.planes[4] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[2]), rows[3].w + rows[2].w));
            fpc.planes[5] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[2]), rows[3].w - rows[2].w));
        }
        fpc.count = m_GDRCtx->InstanceCount;
        vkCmdPushConstants(vkCmd, m_CullLayout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fpc), &fpc);

        uint32_t groupCount = (m_GDRCtx->InstanceCount + 63) / 64;
        vkCmdDispatch(vkCmd, groupCount, 1, 1);

        // ── Compute → Indirect barrier ──
        VkBufferMemoryBarrier indirectBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        indirectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        indirectBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        indirectBarrier.buffer = m_OpaqueIndirectBuffer->GetBuffer(frameIdx);
        indirectBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
            0, nullptr, 1, &indirectBarrier, 0, nullptr);
        indirectBarrier.buffer = m_MaskedIndirectBuffer->GetBuffer(frameIdx);
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
            0, nullptr, 1, &indirectBarrier, 0, nullptr);

        // ── Render pass + indirect draws ──
        cmd.BeginRenderPass(shadowFBO, true, glm::vec4(1.0f));
        vkCmdBindIndexBuffer(vkCmd, pool.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

        struct { glm::mat4 lsm; } shadowPC{ lightSpaceMatrix };

        // Opaque draw (VS-only, hardware Early-Z)
        cmd.BindPipeline(m_GDR_OpaquePipeline);
        auto opaquePipe = std::dynamic_pointer_cast<VulkanPipeline>(m_GDR_OpaquePipeline);
        if (opaquePipe) {
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                opaquePipe->GetVulkanPipelineLayout(),
                2, 1, &m_GDRCtx->Set2Descriptors[frameIdx], 0, nullptr);
            vkCmdPushConstants(vkCmd, opaquePipe->GetVulkanPipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(shadowPC), &shadowPC);
        }
        vkCmdDrawIndexedIndirectCount(vkCmd,
            m_OpaqueIndirectBuffer->GetBuffer(frameIdx), 0,
            m_OpaqueCountBuffer->GetBuffer(frameIdx), 0,
            kGDRMaxInstances,
            sizeof(VkDrawIndexedIndirectCommand));

        // Masked draw (with alpha-test fragment shader + bindless textures)
        cmd.BindPipeline(m_GDR_MaskedPipeline);
        auto maskedPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_GDR_MaskedPipeline);
        if (maskedPipe) {
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                maskedPipe->GetVulkanPipelineLayout(),
                2, 1, &m_GDRCtx->Set2Descriptors[frameIdx], 0, nullptr);
            vkCmdPushConstants(vkCmd, maskedPipe->GetVulkanPipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(shadowPC), &shadowPC);
        }
        vkCmdDrawIndexedIndirectCount(vkCmd,
            m_MaskedIndirectBuffer->GetBuffer(frameIdx), 0,
            m_MaskedCountBuffer->GetBuffer(frameIdx), 0,
            kGDRMaxInstances,
            sizeof(VkDrawIndexedIndirectCommand));

        // ── GDR debug stats ──
        if (m_GDRCtx) {
            context.Stats.DrawCalls += 2;
            context.Stats.ShaderBinds += 2;
            context.Stats.TriangleCount += m_GDRCtx->TotalTriangles;
            context.Stats.VertexCount += m_GDRCtx->TotalVertices;
            context.RecordAndCheckDrawCall("Shadow Pass", "GDR Opaque", "shadow_gdr_opaque",
                m_GDRCtx->TotalTriangles);
            context.RecordAndCheckDrawCall("Shadow Pass", "GDR Masked", "shadow_gdr_masked",
                m_GDRCtx->TotalTriangles);
            cmd.RecordIndirectDraw(2, m_GDRCtx->TotalTriangles * 2);
        }

        cmd.EndRenderPass();

        context.Set("LightSpaceMatrix", lightSpaceMatrix);
        context.Set("ShadowMap_Output", shadowFBO);
    }

    // ═══════════════════════════════════════════════════════════════
    // Fixed-slot path: MoltenVK/M1 fallback (no drawIndirectCount)
    // Uses fixed-slot binning + vkCmdDrawIndexedIndirect
    // ═══════════════════════════════════════════════════════════════
    void VulkanShadowPass::ExecuteGDR_Fixed(RenderContext& context, RenderCommandBuffer& cmd,
                                             VulkanContext* vkCtx, uint32_t frameIdx,
                                             const glm::mat4& lightSpaceMatrix) {
        auto shadowFBO = context.GetFramebuffer("ShadowMap");
        if (!shadowFBO) return;

        auto lightView = context.ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();
        bool hasLight = lightView.begin() != lightView.end();

        VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
        auto& pool = vkCtx->GetGeometryPool();

        // ── Host → Device barrier (shared SSBO + geometry pool data) ──
        VkMemoryBarrier hostBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        hostBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                                   | VK_ACCESS_INDEX_READ_BIT;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

        // ── Compute culling dispatch (fixed-slot variant) ──
        vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipelineFixed);
        m_GDRCtx->BindSet2(vkCmd, m_CullLayoutFixed, VK_PIPELINE_BIND_POINT_COMPUTE, frameIdx);
        vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_CullLayoutFixed, 3, 1, &m_ShadowSet3DescriptorsFixed[frameIdx], 0, nullptr);

        struct FrustumPush { glm::vec4 planes[6]; uint32_t count; uint32_t lightModeMask; uint32_t overrideInstanceID; uint32_t _pad; } fpc;
        fpc.lightModeMask = (uint32_t)context.Get<int>("Shadow.LightMode", 2); // default ShadowCaster
        fpc.overrideInstanceID = 0xFFFFFFFF;
        if (hasLight) {
            glm::mat4 vpT = glm::transpose(lightSpaceMatrix);
            glm::vec4 rows[4] = { vpT[0], vpT[1], vpT[2], vpT[3] };
            fpc.planes[0] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[0]), rows[3].w + rows[0].w));
            fpc.planes[1] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[0]), rows[3].w - rows[0].w));
            fpc.planes[2] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[1]), rows[3].w + rows[1].w));
            fpc.planes[3] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[1]), rows[3].w - rows[1].w));
            fpc.planes[4] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[2]), rows[3].w + rows[2].w));
            fpc.planes[5] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[2]), rows[3].w - rows[2].w));
        }
        fpc.count = m_GDRCtx->InstanceCount;
        vkCmdPushConstants(vkCmd, m_CullLayoutFixed,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fpc), &fpc);

        uint32_t groupCount = (m_GDRCtx->InstanceCount + 63) / 64;
        vkCmdDispatch(vkCmd, groupCount, 1, 1);

        // ── Compute → Indirect barrier ──
        VkBufferMemoryBarrier indirectBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        indirectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        indirectBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        indirectBarrier.buffer = m_OpaqueIndirectBuffer->GetBuffer(frameIdx);
        indirectBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
            0, nullptr, 1, &indirectBarrier, 0, nullptr);
        indirectBarrier.buffer = m_MaskedIndirectBuffer->GetBuffer(frameIdx);
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
            0, nullptr, 1, &indirectBarrier, 0, nullptr);

        // ── Render pass + indirect draws (fixed-slot: draw ALL slots) ──
        cmd.BeginRenderPass(shadowFBO, true, glm::vec4(1.0f));
        vkCmdBindIndexBuffer(vkCmd, pool.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

        struct { glm::mat4 lsm; } shadowPC{ lightSpaceMatrix };

        // Opaque draw — vkCmdDrawIndexedIndirect (no count buffer)
        cmd.BindPipeline(m_GDR_OpaquePipeline);
        auto opaquePipe = std::dynamic_pointer_cast<VulkanPipeline>(m_GDR_OpaquePipeline);
        if (opaquePipe) {
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                opaquePipe->GetVulkanPipelineLayout(),
                2, 1, &m_GDRCtx->Set2Descriptors[frameIdx], 0, nullptr);
            vkCmdPushConstants(vkCmd, opaquePipe->GetVulkanPipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(shadowPC), &shadowPC);
        }
        vkCmdDrawIndexedIndirect(vkCmd,
            m_OpaqueIndirectBuffer->GetBuffer(frameIdx), 0,
            m_GDRCtx->InstanceCount,
            sizeof(VkDrawIndexedIndirectCommand));

        // Masked draw — vkCmdDrawIndexedIndirect (no count buffer)
        cmd.BindPipeline(m_GDR_MaskedPipeline);
        auto maskedPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_GDR_MaskedPipeline);
        if (maskedPipe) {
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                maskedPipe->GetVulkanPipelineLayout(),
                2, 1, &m_GDRCtx->Set2Descriptors[frameIdx], 0, nullptr);
            vkCmdPushConstants(vkCmd, maskedPipe->GetVulkanPipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(shadowPC), &shadowPC);
        }
        vkCmdDrawIndexedIndirect(vkCmd,
            m_MaskedIndirectBuffer->GetBuffer(frameIdx), 0,
            m_GDRCtx->InstanceCount,
            sizeof(VkDrawIndexedIndirectCommand));

        // ── GDR debug stats ──
        if (m_GDRCtx) {
            context.Stats.DrawCalls += 2;
            context.Stats.ShaderBinds += 2;
            context.Stats.TriangleCount += m_GDRCtx->TotalTriangles;
            context.Stats.VertexCount += m_GDRCtx->TotalVertices;
            context.RecordAndCheckDrawCall("Shadow Pass", "GDR Opaque", "shadow_gdr_opaque",
                m_GDRCtx->TotalTriangles);
            context.RecordAndCheckDrawCall("Shadow Pass", "GDR Masked", "shadow_gdr_masked",
                m_GDRCtx->TotalTriangles);
            cmd.RecordIndirectDraw(2, m_GDRCtx->TotalTriangles * 2);
        }

        cmd.EndRenderPass();

        context.Set("LightSpaceMatrix", lightSpaceMatrix);
        context.Set("ShadowMap_Output", shadowFBO);
    }

    // ═══════════════════════════════════════════════════════════════
    // Legacy CPU-driven path (fallback)
    // ═══════════════════════════════════════════════════════════════
    void VulkanShadowPass::ExecuteCPU(RenderContext& context, RenderCommandBuffer& cmd) {
        auto shadowFBO = context.GetFramebuffer("ShadowMap");
        if (!shadowFBO) return;

        if (!m_Pipeline) {
            m_PipeSpec.TargetFramebuffer = shadowFBO;
            m_Pipeline = Pipeline::Create(m_PipeSpec);
        }

        auto lightView = context.ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();

        // Scan for active directional lights (respecting visibility toggle)
        glm::vec3 lightDir(0.0f);
        bool hasActiveLight = false;
        for (auto entityID : lightView) {
            Entity entity{entityID, context.ActiveScene.get()};
            if (!entity.IsActiveInHierarchy()) continue;
            auto& tc = lightView.get<TransformComponent>(entityID);
            lightDir = glm::normalize(glm::vec3(tc.GetTransform() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            hasActiveLight = true;
            break;
        }

        if (!hasActiveLight) {
            context.Set("ShadowMap_Output", std::shared_ptr<Framebuffer>(nullptr));
            context.Set("LightSpaceMatrix", glm::mat4(1.0f));
            return;
        }

        glm::mat4 lightSpaceMatrix(1.0f);
        {
            glm::vec3 lightPos = -lightDir * 20.0f;
            glm::mat4 lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 50.0f);
            glm::mat4 depthCorrection = glm::mat4(1.0f);
            depthCorrection[2][2] = 0.5f;
            depthCorrection[3][2] = 0.5f;
            lightProjection = depthCorrection * lightProjection;
            glm::mat4 lightViewMatrix = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            lightSpaceMatrix = lightProjection * lightViewMatrix;
        }

        static_assert(sizeof(VulkanShadowPushConstants) == 128, "Shadow push constant size mismatch");

        cmd.BeginRenderPass(shadowFBO, true, glm::vec4(1.0f));

        cmd.BindPipeline(m_Pipeline);
        context.Stats.ShaderBinds++;

        auto meshView = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshView) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;
                auto& meshComp = entity.GetComponent<MeshRendererComponent>();
                if (!meshComp.CastShadows) continue;
                auto material = AssetManager::GetAsset<Material>(meshComp.MaterialHandle);
                if (material && material->GetBlendMode() == MaterialBlendMode::Translucent) continue;
                auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
                if (!model) continue;

                VulkanShadowPushConstants constants{};
                constants.LightSpaceMatrix = lightSpaceMatrix;
                constants.Transform = entity.GetWorldTransform();
                cmd.PushConstantData(m_Pipeline, &constants, sizeof(VulkanShadowPushConstants));

                for (auto& mesh : model->GetMeshes()) {
                    std::string tag = entity.GetComponent<TagComponent>().Tag;
                    uint32_t tris = mesh->GetIndexCount() / 3;
                    if (context.RecordAndCheckDrawCall("Shadow Pass", tag, "Shadow Map", tris))
                        cmd.DrawIndexed(mesh, mesh->GetIndexCount());
                }
            }

        cmd.EndRenderPass();

        context.Set("LightSpaceMatrix", lightSpaceMatrix);
        context.Set("ShadowMap_Output", shadowFBO);
    }

}
