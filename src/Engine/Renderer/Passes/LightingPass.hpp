#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/VertexArray.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp" 
#include "Engine/Scene/Components.hpp"

namespace Ayaya {

    class LightingPass : public RenderPass {
    public:
        LightingPass();
        virtual ~LightingPass() override = default;

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

        // ==========================================
        // 各种渲染子通道专属的 PSO 管线
        // ==========================================
        std::shared_ptr<Pipeline> m_DeferredPipeline;
        std::shared_ptr<Pipeline> m_SkyboxPipeline;
        std::shared_ptr<Pipeline> m_GridPipeline;
        std::shared_ptr<Pipeline> m_SpritePipeline;
        std::shared_ptr<Pipeline> m_SelectionMeshPipeline;
        std::shared_ptr<Pipeline> m_SelectionSpritePipeline;

        std::shared_ptr<VertexArray> m_EmptyVAO;

        struct SpriteDrawCommand {
            glm::mat4 Transform;
            SpriteRendererComponent SpriteComp;
            float DistanceToCamera;
        };
        std::vector<SpriteDrawCommand> m_SpriteDrawList; 
    };
}