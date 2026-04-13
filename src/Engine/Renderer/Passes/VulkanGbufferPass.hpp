#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Engine/Scene/Entity.hpp"

namespace Ayaya {

    // 【修复1】：重命名为 VulkanGBufferCommandData，避免与原来的 GBufferPass 冲突
    struct VulkanGBufferCommandData {
        glm::mat4 Transform;
        std::shared_ptr<Mesh> MeshAsset;
        std::shared_ptr<Material> MaterialAsset;
        std::shared_ptr<Pipeline> PipelineAsset; // 核心：使用管线对象驱动
        Entity TargetEntity;
        bool CastShadows;
        bool ReceiveShadows;
    };

    // ==========================================
    // 严格对齐的 Push Constant 结构体
    // ==========================================
    struct alignas(16) GBufferPushConstants {
        glm::mat4 Transform;         // 0 - 63
        alignas(16) glm::vec3 Albedo;// 64 - 75 (vec3 在 std430 规范中必须 16 字节对齐)
        float ReceiveShadows;        // 76 - 79
        float Metallic;              // 80 - 83
        float Roughness;             // 84 - 87
        float AO;                    // 88 - 91
        int UseAlbedoMap;            // 92 - 95
        int UseMetallicMap;          // 96 - 99
        int UseRoughnessMap;         // 100 - 103
        int UseAOMap;                // 104 - 107
        int UseNormalMap;            // 108 - 111
    };

    class VulkanGBufferPass : public RenderPass {
    public:
        VulkanGBufferPass();
        virtual ~VulkanGBufferPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Framebuffer> m_GeometryFBO;
        
        std::shared_ptr<Shader> m_GBufferShader;
        std::shared_ptr<Shader> m_FallbackShader;
        std::shared_ptr<Material> m_FallbackMaterial;

        // 核心几何管线与错误材质回退管线
        std::shared_ptr<Pipeline> m_GBufferPipeline;
        std::shared_ptr<Pipeline> m_FallbackPipeline;
        
        std::vector<VulkanGBufferCommandData> m_OpaqueDrawList;
    };

}