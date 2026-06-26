#include "ayapch.h"
#include "GenericComputePass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/PassRegistry.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include "Core/VFS.hpp"

namespace Ayaya {

    GenericComputePass::~GenericComputePass() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();
        vkDeviceWaitIdle(device);
        for (auto& [_, pipeline] : m_PipelineCache) vkDestroyPipeline(device, pipeline, nullptr);
        for (auto& [_, layout]   : m_LayoutCache)   vkDestroyPipelineLayout(device, layout, nullptr);
        for (auto& [_, module]   : m_ShaderCache)    vkDestroyShaderModule(device, module, nullptr);
    }

    void GenericComputePass::OnAttach() {
    }

    void GenericComputePass::DeclareResources(RGBuilder& builder, uint32_t w, uint32_t h,
                                               const PassBakedParams& params) {
    }

    void GenericComputePass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        std::string prefix = m_NodeName.empty() ? m_PassName : m_NodeName;

        // Read Lua params
        std::string shaderPath = context.Get<std::string>(prefix + ".Shader", "");
        std::string dispatchTex = context.Get<std::string>(prefix + ".DispatchBasedOn", "");
        int wgX = context.Get<int>(prefix + ".WorkGroupX", 8);
        int wgY = context.Get<int>(prefix + ".WorkGroupY", 8);
        int wgZ = context.Get<int>(prefix + ".WorkGroupZ", 1);
        int manualX = context.Get<int>(prefix + ".DispatchX", 0);
        int manualY = context.Get<int>(prefix + ".DispatchY", 0);
        int manualZ = context.Get<int>(prefix + ".DispatchZ", 0);

        if (shaderPath.empty()) {
            AYAYA_CORE_WARN("[GenericCompute] No Shader param set");
            return;
        }

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();
        VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();

        // Get or create compute pipeline
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        auto it = m_PipelineCache.find(shaderPath);
        if (it != m_PipelineCache.end()) {
            pipeline = it->second;
            layout = m_LayoutCache[shaderPath];
        } else {
            // Load compute SPIR-V
            auto exePath = std::filesystem::current_path();
            std::string spvPath = (exePath / "assets/Editor/shaders/cache/vulkan" / (shaderPath + ".spv")).string();
            // Also try project-local
            if (!std::filesystem::exists(spvPath)) {
                std::string projLocal = VFS::ResolveString("project://Shaders/Cache/" + shaderPath + ".spv");
                if (std::filesystem::exists(projLocal)) spvPath = projLocal;
            }
            std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                AYAYA_CORE_ERROR("[GenericCompute] Shader not found: {}", shaderPath);
                return;
            }
            size_t sz = (size_t)file.tellg();
            std::vector<char> buf(sz);
            file.seekg(0); file.read(buf.data(), sz); file.close();

            VkShaderModuleCreateInfo sm{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            sm.codeSize = buf.size();
            sm.pCode = reinterpret_cast<const uint32_t*>(buf.data());
            VkShaderModule module;
            if (vkCreateShaderModule(device, &sm, nullptr, &module) != VK_SUCCESS) {
                AYAYA_CORE_ERROR("[GenericCompute] Failed to create shader module: {}", shaderPath);
                return;
            }
            m_ShaderCache[shaderPath] = module;

            VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            plInfo.setLayoutCount = 0;
            plInfo.pSetLayouts = nullptr;
            plInfo.pushConstantRangeCount = 0;
            vkCreatePipelineLayout(device, &plInfo, nullptr, &layout);
            m_LayoutCache[shaderPath] = layout;

            VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpInfo.stage.module = module;
            cpInfo.stage.pName = "main";
            cpInfo.layout = layout;
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &pipeline);
            m_PipelineCache[shaderPath] = pipeline;
        }

        // Dynamic dispatch from reference texture
        uint32_t groupX = (uint32_t)manualX, groupY = (uint32_t)manualY, groupZ = (uint32_t)manualZ;
        if (groupX == 0 && !dispatchTex.empty()) {
            auto fbo = context.GetFramebuffer(dispatchTex);
            if (fbo) {
                auto& spec = fbo->GetSpecification();
                groupX = (spec.Width  + (uint32_t)wgX - 1) / (uint32_t)wgX;
                groupY = (spec.Height + (uint32_t)wgY - 1) / (uint32_t)wgY;
                groupZ = groupZ > 0 ? groupZ : 1;
            }
        }
        if (groupX == 0) groupX = 1;
        if (groupY == 0) groupY = 1;
        if (groupZ == 0) groupZ = 1;

        vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdDispatch(vkCmd, groupX, groupY, groupZ);
    }

} // namespace Ayaya
