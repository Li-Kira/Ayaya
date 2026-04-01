#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/VertexArray.hpp"
#include "Renderer/RenderCommandBuffer.hpp" // 【新增】
#include "Engine/Scene/Components.hpp"

namespace Ayaya {

    class LightingPass : public RenderPass {
    public:
        LightingPass();
        virtual ~LightingPass() override = default; // 析构交给智能指针管理

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override; // 【修改】

    private:
        std::shared_ptr<Framebuffer> m_LightingFBO;
        std::shared_ptr<Framebuffer> m_SelectionFBO;
        
        std::shared_ptr<Shader> m_DeferredLightingShader;
        std::shared_ptr<Shader> m_SkyboxShader;
        std::shared_ptr<Shader> m_GridShader;
        std::shared_ptr<Shader> m_SpriteShader;
        std::shared_ptr<Shader> m_OutlineShader;

        // 【修改】：使用引擎的 VAO 抽象
        std::shared_ptr<VertexArray> m_EmptyVAO;

        struct SpriteDrawCommand {
            glm::mat4 Transform;
            SpriteRendererComponent SpriteComp;
            float DistanceToCamera;
        };
        std::vector<SpriteDrawCommand> m_SpriteDrawList; 
    };
}