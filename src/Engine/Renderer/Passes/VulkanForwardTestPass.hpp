#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Engine/Scene/Entity.hpp"

namespace Ayaya {

    struct VulkanForwardCommandData {
        glm::mat4 Transform;
        std::shared_ptr<Mesh> MeshAsset;
        std::shared_ptr<Material> MaterialAsset;
        std::shared_ptr<Pipeline> PipelineAsset;
        Entity TargetEntity;
    };

    struct ForwardPushConstants {
        glm::mat4 Transform;
        glm::vec4 Albedo;
        int UseAlbedoMap;
        float Metallic;
        float Roughness;
        float AO;
        int UseMetallicMap;
        int UseRoughnessMap;
        int UseAOMap;
        int UseNormalMap;
        float EnvironmentIntensity;
        float _pad0, _pad1, _pad2;
        glm::vec4 EnvironmentAmbientColor;
        glm::mat4 LightSpaceMatrix;
        int EnableShadows;
    };

    class VulkanForwardTestPass : public RenderPass {
    public:
        VulkanForwardTestPass();
        virtual ~VulkanForwardTestPass() override = default;

        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        // 声明该 Pass 对 RenderGraph 的资源依赖 (供 SceneRenderer 调用)
        static void DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        std::shared_ptr<Shader> m_ForwardShader;
        std::shared_ptr<Material> m_DefaultMaterial;
        std::shared_ptr<Pipeline> m_ForwardPipeline;
        PipelineSpecification m_ForwardPipeSpec;

        std::shared_ptr<Shader> m_SkyboxShader;
        std::shared_ptr<Pipeline> m_SkyboxPipeline;
        PipelineSpecification m_SkyboxPipeSpec;

        std::shared_ptr<Shader> m_SpriteShader;
        std::shared_ptr<Pipeline> m_SpritePipeline;
        PipelineSpecification m_SpritePipeSpec;

        std::vector<VulkanForwardCommandData> m_OpaqueDrawList;
    };

}
