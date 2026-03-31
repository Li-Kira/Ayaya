#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Engine/Scene/Components.hpp"

namespace Ayaya {

    class LightingPass : public RenderPass {
    public:
        LightingPass();
        virtual ~LightingPass() override;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context) override;

    private:
        std::shared_ptr<Framebuffer> m_LightingFBO;
        std::shared_ptr<Framebuffer> m_SelectionFBO;
        
        std::shared_ptr<Shader> m_DeferredLightingShader;
        std::shared_ptr<Shader> m_SkyboxShader;
        std::shared_ptr<Shader> m_GridShader;
        std::shared_ptr<Shader> m_SpriteShader;
        std::shared_ptr<Shader> m_OutlineShader;

        uint32_t m_EmptyVAO = 0;

        struct SpriteDrawCommand {
            glm::mat4 Transform;
            SpriteRendererComponent SpriteComp;
            float DistanceToCamera;
        };
        std::vector<SpriteDrawCommand> m_SpriteDrawList; // 容器复用！
    };
}