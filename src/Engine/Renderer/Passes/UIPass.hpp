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
#include <vector>

namespace Ayaya {

    struct UIVertex {
        float px, py, pz;
        float cr, cg, cb, ca;
        float tu, tv;
        float TexIndex;
    };
    static_assert(sizeof(UIVertex) == 40, "UIVertex must be 40 bytes, no padding");

    struct UIRenderData {
        static constexpr uint32_t MaxQuads        = 10000;
        static constexpr uint32_t MaxVertices     = MaxQuads * 6;
        static constexpr uint32_t MaxIndices      = MaxQuads * 6;
        static constexpr uint32_t MaxTextureSlots = 16;

        std::shared_ptr<IndexBuffer> QuadIB;
        std::shared_ptr<Shader>      UIShader;
        std::shared_ptr<Texture2D>   WhiteTexture;

        uint32_t  QuadIndexCount = 0;
        UIVertex* QuadVertexBufferBase = nullptr;
        UIVertex* QuadVertexBufferPtr  = nullptr;

        std::array<std::shared_ptr<Texture2D>, MaxTextureSlots> TextureSlots;
        uint32_t TextureSlotIndex = 1;
        std::shared_ptr<Texture2D> ActiveTexture;

        glm::vec4 QuadVertexPositions[4];
    };

    class UIPass : public RenderPass {
    public:
        UIPass();
        ~UIPass() override;

        void Init();
        static void Shutdown();

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override {}
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

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
        std::shared_ptr<Pipeline> m_UIPipeline;
        std::shared_ptr<Framebuffer> m_UIFBO;

        uint32_t m_ViewportWidth = 1280;
        uint32_t m_ViewportHeight = 720;

        // Triple-buffer GC: per-instance, 防止同一帧内多个 renderer 互相踩踏
        std::vector<std::shared_ptr<VertexBuffer>> m_VBGCTrash[3];
        std::vector<std::shared_ptr<VertexArray>>  m_VAGCTrash[3];
    };

}
