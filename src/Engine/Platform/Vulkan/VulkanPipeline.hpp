#pragma once
#include "Renderer/Pipeline.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <unordered_map>

namespace Ayaya {

    class VulkanPipeline : public Pipeline {
    public:
        VulkanPipeline(const PipelineSpecification& spec);
        virtual ~VulkanPipeline() override;

        virtual const PipelineSpecification& GetSpecification() const override { return m_Specification; }
        virtual PipelineSpecification& GetSpecification() override { return m_Specification; }
        virtual void Bind() override {} 

        VkPipeline GetVulkanPipeline() const { return m_Pipeline; }
        VkPipelineLayout GetVulkanPipelineLayout() const {
            return m_CustomLayout != VK_NULL_HANDLE ? m_CustomLayout : m_PipelineLayout;
        }
        // Override the reflected pipeline layout (e.g. for instanced set=2 SSBO).
        void SetCustomLayout(VkPipelineLayout layout) { m_CustomLayout = layout; }
        VkPipelineLayout GetCustomLayout() const { return m_CustomLayout; }
        
        // ==========================================
        // 【核心修改】：现在需要传入 frameIndex 来获取对应帧的相机描述符集 (Set 0)
        // ==========================================
        VkDescriptorSet GetVulkanDescriptorSet(uint32_t setIndex, uint32_t frameIndex = 0) const {
            if (setIndex == 0) {
                // 增加安全取模，防止 frameIndex 超过 m_GlobalDescriptorSets 的大小
                size_t size = m_GlobalDescriptorSets.size();
                return size > 0 ? m_GlobalDescriptorSets[frameIndex % size] : VK_NULL_HANDLE;
            }
            return VK_NULL_HANDLE;
        }

        VkDescriptorSetLayout GetDescriptorSetLayout(uint32_t setIndex) const {
            return setIndex < m_DescriptorSetLayouts.size() ? m_DescriptorSetLayouts[setIndex] : VK_NULL_HANDLE;
        }

        uint32_t GetTextureSetIndex() const { return m_TextureSetIndex; }

        // 返回 layout 中描述符集数量，用于动态判断纹理在 Set 0 还是 Set 1
        uint32_t GetDescriptorSetLayoutCount() const {
            uint32_t count = 0;
            for (auto& layout : m_DescriptorSetLayouts) {
                if (layout != VK_NULL_HANDLE) count++;
            }
            return count;
        }

        // 注册 UBO，需指定是哪一帧的 Buffer
        static void SetGlobalUniformBuffer(uint32_t binding, uint32_t frameIndex, VkBuffer buffer, uint32_t size);
        static void ClearGlobalUBOs() { s_GlobalUBOs.clear(); }

        // Extra descriptor-set layouts appended after the reflected ones.
        // Set before calling Pipeline::Create(), cleared automatically.
        static std::vector<VkDescriptorSetLayout> s_ExtraSetLayouts;

        // 重新写入 UBO 描述符集 (多实例 s_GlobalUBOs 竞争修复)
        void RefreshDescriptorSets(VkDevice device);

        // ==========================================
        // 环形缓冲，每次索要都给一个全新的描述符集！(Set 1)
        // ==========================================
        VkDescriptorSet GetNextTextureDescriptorSet() {
            if (m_TextureDescriptorSets.empty()) return VK_NULL_HANDLE;
            uint32_t index = m_CurrentTextureSetIndex;
            m_CurrentTextureSetIndex = (m_CurrentTextureSetIndex + 1) % m_TextureDescriptorSets.size();
            return m_TextureDescriptorSets[index];
        }

    private:
        // 记录每一帧对应的 UBO 缓冲区信息 [Binding] -> [Frame0_Info, Frame1_Info]
        static std::unordered_map<uint32_t, std::array<VkDescriptorBufferInfo, 3>> s_GlobalUBOs;
        
        PipelineSpecification m_Specification;

        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_CustomLayout = VK_NULL_HANDLE;  // overridden for instancing
        
        std::array<VkDescriptorSetLayout, 2> m_DescriptorSetLayouts = { VK_NULL_HANDLE, VK_NULL_HANDLE };

        // 【核心修改】：Set 0 变为数组，为每一帧保存一份专属的描述符
        std::vector<VkDescriptorSet> m_GlobalDescriptorSets; 

        // 专属池与环形缓冲
        VkDescriptorPool m_PipelineDescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_TextureDescriptorSets;
        uint32_t m_CurrentTextureSetIndex = 0;
        uint32_t m_TextureSetIndex = 1;
    };

}