#pragma once

#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Buffer.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/VertexArray.hpp"
#include "Renderer/Pipeline.hpp"

#include <glm/glm.hpp>
#include <array>
#include <memory>

namespace Ayaya {

    // 动态合并的 UI 顶点结构
    struct UIVertex {
        glm::vec3 Position;
        glm::vec4 Color;
        glm::vec2 TexCoord;
    };

    struct UIRenderData {
        static constexpr uint32_t MaxQuads        = 10000;
        static constexpr uint32_t MaxVertices     = MaxQuads * 4;
        static constexpr uint32_t MaxIndices      = MaxQuads * 6;
        static constexpr uint32_t MaxTextureSlots = 16;

        std::shared_ptr<VertexArray> QuadVA;
        std::shared_ptr<IndexBuffer> QuadIB;
        std::shared_ptr<Shader>      UIShader;
        std::shared_ptr<Texture2D>   WhiteTexture;

        uint32_t  QuadIndexCount = 0;
        UIVertex* QuadVertexBufferBase = nullptr;
        UIVertex* QuadVertexBufferPtr  = nullptr;

        std::array<std::shared_ptr<Texture2D>, MaxTextureSlots> TextureSlots;
        uint32_t TextureSlotIndex = 1; // slot 0 = WhiteTexture
        std::shared_ptr<Texture2D> ActiveTexture; // 当前批次绑定的纹理（null = WhiteTexture）

        glm::vec4 QuadVertexPositions[4];
    };

    class UIPass : public RenderPass {
    public:
        UIPass();
        ~UIPass() override;

        void Init();
        static void Shutdown();

        virtual void OnAttach() override {}
        virtual void OnResize(uint32_t width, uint32_t height) override {}
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        // 批处理接口
        void StartBatch();
        void Flush(RenderCommandBuffer& cmd);
        void NextBatch(RenderCommandBuffer& cmd);
        void DrawUIElement(RenderCommandBuffer& cmd,
                           const glm::mat4& renderTransform,
                           const glm::vec4& color,
                           std::shared_ptr<Texture2D> texture,
                           int entityID);

    private:
        bool m_Initialized = false;
        std::shared_ptr<Framebuffer> m_UIFBO;
        std::shared_ptr<Pipeline>   m_UIPipeline;
        uint32_t m_UIFBOWidth = 0, m_UIFBOHeight = 0;
    };

}
