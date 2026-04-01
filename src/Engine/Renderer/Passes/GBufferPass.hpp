#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/RenderCommandBuffer.hpp" // 【新增】
#include "Engine/Scene/Entity.hpp"

namespace Ayaya {

    struct RenderCommandData {
        glm::mat4 Transform;
        std::shared_ptr<Mesh> MeshAsset;
        std::shared_ptr<Material> MaterialAsset;
        std::shared_ptr<Shader> ShaderAsset;
        Entity TargetEntity;
        bool CastShadows;
        bool ReceiveShadows;
    };

    class GBufferPass : public RenderPass {
    public:
        GBufferPass();
        virtual ~GBufferPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Framebuffer> m_GeometryFBO;
        std::shared_ptr<Shader> m_GBufferShader;
        std::shared_ptr<Shader> m_FallbackShader;
        std::shared_ptr<Material> m_FallbackMaterial;
        
        std::vector<RenderCommandData> m_OpaqueDrawList;
    };

}