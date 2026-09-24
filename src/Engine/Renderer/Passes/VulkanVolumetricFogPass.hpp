#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Platform/Vulkan/VulkanVolumeTexture.hpp"
#include "Platform/Vulkan/VulkanStorageBuffer.hpp"
#include <array>
#include <vk_mem_alloc.h>

namespace Ayaya {

    // Froxel volumetric fog (Frostbite/UE style). 3D texture aligned to the camera
    // frustum, light injection + transmittance integration in compute, composited as
    // a fullscreen pass reading the HDR scene color + depth. The froxel volumes live
    // OUTSIDE the RenderGraph (triple-buffered, like the GBuffer Hi-Z pyramid).
    class VulkanVolumetricFogPass : public RenderPass {
    public:
        VulkanVolumetricFogPass() { m_PassName = "Volumetric Fog"; }
        ~VulkanVolumetricFogPass() override;
        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        // Froxel grid — keep in sync with the Volumetric/*.comp/.frag defines.
        static constexpr uint32_t kFroxelX = 160;
        static constexpr uint32_t kFroxelY = 90;
        static constexpr uint32_t kFroxelZ = 64;

        // GPU-side uniforms (Set 0 for both compute passes).
        struct FogUniforms {
            glm::mat4 InvViewProj;    // 0
            glm::vec4 CameraPosNear;  // 64  (xyz = camera pos, w = near)
            glm::vec4 ScreenFar;      // 80  (x = width, y = height, z = far)
            glm::vec4 DirLightDir;    // 96
            glm::vec4 DirLightColor;  // 112
            glm::vec4 Fog;            // 128 (x = density, y = heightFalloff, z = phaseG, w = maxDist)
            glm::vec4 ScatterColor;   // 144
            glm::vec4 PointCount;     // 160 (x = point light count)
            glm::vec4 FogHeight;      // 176 (x = fog layer world-Y anchor — entity transform)
            glm::vec4 LocalFogCount;  // 192 (x = local fog volume count)
        }; // 208 bytes
        std::unique_ptr<VulkanStorageBuffer> m_ParamsSSBO;

        // Triple-buffered froxel volumes: scattering (A) + integrated (B).
        struct VolumePair {
            VulkanVolumeTexture scattering;
            VulkanVolumeTexture integrated;
        };
        std::array<VolumePair, 3> m_Volumes;

        // ── Compute: injection ──
        VkDescriptorSetLayout m_InjectionSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_InjectionPool = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, 3> m_InjectionSets{};
        VkPipelineLayout m_InjectionLayout = VK_NULL_HANDLE;
        VkPipeline       m_InjectionPipeline = VK_NULL_HANDLE;
        VkShaderModule   m_InjectionShader = VK_NULL_HANDLE;

        // ── Compute: accumulation ──
        VkDescriptorSetLayout m_AccumSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_AccumPool = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, 3> m_AccumSets{};
        VkPipelineLayout m_AccumLayout = VK_NULL_HANDLE;
        VkPipeline       m_AccumPipeline = VK_NULL_HANDLE;
        VkShaderModule   m_AccumShader = VK_NULL_HANDLE;

        // ── Apply fullscreen (graphics) ──
        std::shared_ptr<Shader>      m_ApplyShader;
        std::shared_ptr<Framebuffer> m_ApplyRefFBO;
        std::shared_ptr<Pipeline>    m_ApplyPipeline;
        VkDescriptorSetLayout m_ApplySet2Layout = VK_NULL_HANDLE; // sampler3D volume B
        VkDescriptorPool      m_ApplySet2Pool = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, 3> m_ApplySet2{};

        // Dummy SSBO bound when the point-light SSBO is absent (keeps descriptors valid).
        std::unique_ptr<VulkanStorageBuffer> m_DummySSBO;

        uint32_t m_LastW = 0, m_LastH = 0;
        bool m_Initialized = false;

        void CreateVolumes(VkDevice device, VmaAllocator allocator);
        void ReleaseVolumes();
        void CreateComputeResources(VkDevice device, VmaAllocator allocator, uint32_t framesInFlight);
        void DestroyComputeResources();
        void CreateApplyResources(VkDevice device, uint32_t framesInFlight);
        void DestroyApplyResources();
        void RewriteDescriptors(VkDevice device, uint32_t framesInFlight);
        VkShaderModule LoadComputeShader(VkDevice device, const char* relPath);
    };

} // namespace Ayaya
