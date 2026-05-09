#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Engine/Scene/Entity.hpp"

namespace Ayaya {

    // ==========================================
    // 专门为当前 Pass 定义的命令数据包
    // ==========================================
    struct VulkanForwardCommandData {
        glm::mat4 Transform;
        std::shared_ptr<Mesh> MeshAsset;
        std::shared_ptr<Material> MaterialAsset;
        std::shared_ptr<Pipeline> PipelineAsset;
        Entity TargetEntity;
    };

    // ==========================================
    // 【核心修复 3】：严格对齐的 Push Constant
    // 遵循 std430 规范，保证显卡不会读错内存
    // ==========================================
    // 使用 vec4 代替 alignas(16) vec3，避免编译器 padding 导致 C++/GLSL 偏移不一致
    struct ForwardPushConstants {
        glm::mat4 Transform;           // offset 0,  size 64
        glm::vec4 Albedo;              // offset 64, size 16 (natural 16-alignment)
        int UseAlbedoMap;              // offset 80
        float Metallic;                // offset 84
        float Roughness;               // offset 88
        float AO;                      // offset 92
        int UseMetallicMap;            // offset 96
        int UseRoughnessMap;           // offset 100
        int UseAOMap;                  // offset 104
        int UseNormalMap;              // offset 108
        float EnvironmentIntensity;    // offset 112
        float _pad0;                   // offset 116 — 手动对齐 padding
        float _pad1;                   // offset 120
        float _pad2;                   // offset 124
        glm::vec4 EnvironmentAmbientColor; // offset 128, size 16
    };

    class VulkanForwardTestPass : public RenderPass {
    public:
        VulkanForwardTestPass();
        virtual ~VulkanForwardTestPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        // 【核心修改】：将单一 FBO 升级为多帧隔离的 FBO 数组
        std::vector<std::shared_ptr<Framebuffer>> m_ForwardFBOs;

        std::shared_ptr<Shader> m_ForwardShader;
        std::shared_ptr<Material> m_DefaultMaterial;
        std::shared_ptr<Pipeline> m_ForwardPipeline;

        std::shared_ptr<Shader> m_SkyboxShader;
        std::shared_ptr<Pipeline> m_SkyboxPipeline;

        // 选中描边
        std::shared_ptr<Framebuffer> m_SelectionFBO;
        std::shared_ptr<Shader> m_OutlineShader;
        std::shared_ptr<Pipeline> m_OutlinePipeline;

        std::vector<VulkanForwardCommandData> m_OpaqueDrawList;
    };

}
