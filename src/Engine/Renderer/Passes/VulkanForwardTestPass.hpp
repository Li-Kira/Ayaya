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
    struct alignas(16) ForwardPushConstants {
        glm::mat4 Transform;         // 64 bytes (0 - 63)
        alignas(16) glm::vec3 Albedo;// 12 bytes (64 - 75)
        int UseAlbedoMap;            // 4 bytes  (76 - 79)
        
        // --- 新增的光源与材质参数 ---
        alignas(16) glm::vec3 LightDir;   // 12 bytes (80 - 91)
        float Metallic;                   // 4 bytes  (92 - 95)
        alignas(16) glm::vec3 LightColor; // 12 bytes (96 - 107)
        float Roughness;                  // 4 bytes  (108 - 111)
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
        std::vector<VulkanForwardCommandData> m_OpaqueDrawList;
    };

}