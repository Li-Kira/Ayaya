#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp" 
#include "Engine/Scene/Components.hpp"

namespace Ayaya {

    // ==========================================
    // 【核心重构】：定义与 Shader 严格对齐的 Push Constant 结构体
    // ==========================================
    struct alignas(16) DeferredLightingPushConstants {
        glm::mat4 LightSpaceMatrix; // 64 bytes (0 - 63)
        alignas(16) glm::vec3 AmbientColor;     // 12 bytes (64 - 75)
        float Intensity;            // 4 bytes  (76 - 79)
        int EnvMapEnabled;          // 4 bytes  (80 - 83)
    };

    struct alignas(16) SkyboxPushConstants {
        glm::mat4 Projection;       // 64 bytes (0 - 63)
        glm::mat4 View;             // 64 bytes (64 - 127)
        glm::mat4 Transform;        // 64 bytes (128 - 191)
        float Intensity;            // 4 bytes  (192 - 195)
    };

    struct alignas(16) GridPushConstants {
        glm::mat4 Transform;        // 64 bytes (0 - 63)
        float ExposureInverse;      // 4 bytes  (64 - 67)
    };

    struct alignas(16) SpritePushConstants {
        glm::mat4 Transform;        // 64 bytes (0 - 63)
        glm::vec4 Color;            // 16 bytes (64 - 79)
        float ExposureInverse;      // 4 bytes  (80 - 83)
        int UseTexture;             // 4 bytes  (84 - 87)
    };

    struct alignas(16) OutlinePushConstants {
        glm::mat4 Transform;        // 64 bytes (0 - 63)
        alignas(16) glm::vec3 Color;            // 12 bytes (64 - 75)
    };

    class VulkanLightingPass : public RenderPass {
    public:
        VulkanLightingPass();
        virtual ~VulkanLightingPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Framebuffer> m_LightingFBO;
        std::shared_ptr<Framebuffer> m_SelectionFBO;
        
        std::shared_ptr<Shader> m_DeferredLightingShader;
        std::shared_ptr<Shader> m_SkyboxShader;
        std::shared_ptr<Shader> m_GridShader;
        std::shared_ptr<Shader> m_SpriteShader;
        std::shared_ptr<Shader> m_OutlineShader;

        std::shared_ptr<Material> m_DeferredMaterial;

        // 各种渲染子通道专属的 PSO 管线
        std::shared_ptr<Pipeline> m_DeferredPipeline;
        std::shared_ptr<Pipeline> m_SkyboxPipeline;
        std::shared_ptr<Pipeline> m_GridPipeline;
        std::shared_ptr<Pipeline> m_SpritePipeline;
        std::shared_ptr<Pipeline> m_SelectionMeshPipeline;
        std::shared_ptr<Pipeline> m_SelectionSpritePipeline;

        struct SpriteDrawCommand {
            glm::mat4 Transform;
            SpriteRendererComponent SpriteComp;
            float DistanceToCamera;
        };

        struct OutlineMeshCommand {
            glm::mat4 Transform;
            std::shared_ptr<Mesh> MeshAsset;
        };

        std::vector<SpriteDrawCommand> m_SpriteDrawList;
        std::vector<OutlineMeshCommand> m_SelectionMeshDrawList;
        std::vector<SpriteDrawCommand> m_SelectionSpriteDrawList;

        std::shared_ptr<Mesh> s_SkyboxMesh;
    };

}