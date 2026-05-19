#include "ayapch.h"
#include "UIPass.hpp"
#include "Scene/Components.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/UILayoutSystem.hpp"
#include "Asset/AssetManager.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    static UIRenderData s_Data;

    UIPass::UIPass() {}
    UIPass::~UIPass() {}

    void UIPass::Shutdown() {
        delete[] s_Data.QuadVertexBufferBase;
        s_Data.QuadVertexBufferBase = nullptr;
        s_Data.QuadVA.reset();
        s_Data.QuadIB.reset();
        s_Data.UIShader.reset();
        s_Data.WhiteTexture.reset();
        for (auto& tex : s_Data.TextureSlots) tex.reset();
        s_Data.ActiveTexture.reset();
        s_Data.QuadIndexCount = 0;
        AYAYA_CORE_INFO("UIPass: Shutdown complete");
    }

    void UIPass::Init() {
        if (m_Initialized || s_Data.WhiteTexture) return; // s_Data 是 static，全局只初始化一次

        s_Data.QuadVertexBufferBase = new UIVertex[UIRenderData::MaxVertices];
        s_Data.UIShader = Shader::Create("UI/ui.vert", "UI/ui.frag");

        // 预计算索引缓冲 — 每 Quad 两个三角形，只创建一次
        auto* quadIndices = new uint32_t[UIRenderData::MaxIndices];
        uint32_t offset = 0;
        for (uint32_t i = 0; i < UIRenderData::MaxIndices; i += 6) {
            quadIndices[i + 0] = offset + 0;
            quadIndices[i + 1] = offset + 1;
            quadIndices[i + 2] = offset + 2;
            quadIndices[i + 3] = offset + 2;
            quadIndices[i + 4] = offset + 3;
            quadIndices[i + 5] = offset + 0;
            offset += 4;
        }
        s_Data.QuadIB = IndexBuffer::Create(quadIndices, UIRenderData::MaxIndices);
        delete[] quadIndices;

        // 1x1 白色回退纹理
        s_Data.WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whitePixel = 0xFFFFFFFF;
        s_Data.WhiteTexture->SetData(&whitePixel, sizeof(whitePixel));
        s_Data.TextureSlots[0] = s_Data.WhiteTexture;

        // 单位四边形顶点 (左下角原点)
        s_Data.QuadVertexPositions[0] = { 0.0f, 0.0f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[1] = { 1.0f, 0.0f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[2] = { 1.0f, 1.0f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[3] = { 0.0f, 1.0f, 0.0f, 1.0f };

        m_Initialized = true;
        // Vulkan: 预创建 UI Pipeline（alpha blending，无深度，无 culling）
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan && !m_UIPipeline) {
            PipelineSpecification pipeSpec;
            pipeSpec.Shader = s_Data.UIShader;
            pipeSpec.Layout = {
                { ShaderDataType::Float3, "a_Position" },
                { ShaderDataType::Float4, "a_Color"    },
                { ShaderDataType::Float2, "a_TexCoord" },
            };
            pipeSpec.DepthTest = false;
            pipeSpec.DepthWrite = false;
            pipeSpec.Blend = true;
            pipeSpec.BackfaceCulling = CullMode::None;
            m_UIPipeline = Pipeline::Create(pipeSpec);
        }

        AYAYA_CORE_INFO("UIPass: Initialized (max {0} quads, {1} texture slots)",
                        UIRenderData::MaxQuads, UIRenderData::MaxTextureSlots);
    }

    void UIPass::StartBatch() {
        s_Data.QuadIndexCount = 0;
        s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
        s_Data.ActiveTexture = nullptr;
    }

    void UIPass::Flush(RenderCommandBuffer& cmd) {
        if (s_Data.QuadIndexCount == 0) return;

        uint32_t vertexCount = (uint32_t)(s_Data.QuadVertexBufferPtr - s_Data.QuadVertexBufferBase);
        uint32_t dataSize = vertexCount * sizeof(UIVertex);

        auto vb = VertexBuffer::Create((float*)s_Data.QuadVertexBufferBase, dataSize);
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float4, "a_Color"    },
            { ShaderDataType::Float2, "a_TexCoord" },
        });

        auto va = VertexArray::Create();
        va->AddVertexBuffer(vb);
        va->SetIndexBuffer(s_Data.QuadIB);

        float w = (float)Application::Get().GetWindow().GetWidth();
        float h = (float)Application::Get().GetWindow().GetHeight();
        glm::mat4 ortho = glm::ortho(0.0f, w, h, 0.0f, -1.0f, 1.0f);

        s_Data.UIShader->Bind();
        s_Data.UIShader->SetMat4("u_ViewProjection", ortho);

        // 绑定当前批次激活的纹理（无纹理时回退到 WhiteTexture）
        auto& tex = s_Data.ActiveTexture ? s_Data.ActiveTexture : s_Data.WhiteTexture;
        tex->Bind(0);

        va->Bind();
        cmd.DrawIndexed(va, s_Data.QuadIndexCount);
    }

    void UIPass::NextBatch(RenderCommandBuffer& cmd) {
        Flush(cmd);
        StartBatch();
    }

    void UIPass::DrawUIElement(RenderCommandBuffer& cmd,
                               const glm::mat4& renderTransform,
                               const glm::vec4& color,
                               std::shared_ptr<Texture2D> texture,
                               int entityID) {
        // 纹理切换 → flush 当前批次，新纹理开始新批次
        if (texture != s_Data.ActiveTexture) {
            if (texture && s_Data.ActiveTexture &&
                texture->GetRendererID() == s_Data.ActiveTexture->GetRendererID()) {
                // 不同 shared_ptr 但同一纹理（同一 RendererID），不切换
            } else {
                Flush(cmd);
                StartBatch();
                s_Data.ActiveTexture = texture;
            }
        }

        if (s_Data.QuadIndexCount >= UIRenderData::MaxIndices) {
            NextBatch(cmd);
            s_Data.ActiveTexture = texture;
        }

        constexpr glm::vec2 texCoords[] = {
            { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
        };

        for (size_t i = 0; i < 4; i++) {
            glm::vec4 pos = renderTransform * s_Data.QuadVertexPositions[i];
            s_Data.QuadVertexBufferPtr->Position = glm::vec3(pos.x, pos.y, 0.0f);
            s_Data.QuadVertexBufferPtr->Color    = color;
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr++;
        }

        s_Data.QuadIndexCount += 6;
    }

    void UIPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        Init();

        auto* scene = context.ActiveScene.get();
        if (!scene) return;

        UILayoutSystem::Update(*scene);

        StartBatch();

        auto canvasView = scene->Reg().view<CanvasComponent>();
        for (auto canvasEntity : canvasView) {
            Entity canvas{ canvasEntity, scene };
            if (!canvas.HasComponent<RelationshipComponent>()) continue;

            std::function<void(Entity)> collectUI = [&](Entity parent) {
                auto& rel = parent.GetComponent<RelationshipComponent>();
                for (auto childID : rel.Children) {
                    Entity child{ childID, scene };
                    if (!child.HasComponent<RectTransformComponent>()) continue;
                    auto& rt = child.GetComponent<RectTransformComponent>();

                    if (child.HasComponent<UIImageComponent>()) {
                        auto& img = child.GetComponent<UIImageComponent>();
                        std::shared_ptr<Texture2D> tex;
                        if (img.TextureHandle != 0)
                            tex = AssetManager::GetAsset<Texture2D>(img.TextureHandle);
                        DrawUIElement(cmd, rt.RenderTransform, img.Color, tex,
                                      (int)entt::to_integral(childID));
                    }
                    if (child.HasComponent<UITextComponent>()) { /* TODO */ }
                    if (child.HasComponent<RelationshipComponent>()) collectUI(child);
                }
            };
            collectUI(canvas);
        }

        if (s_Data.QuadIndexCount == 0) return;

        // 查找最终场景 FBO 用于叠加
        std::shared_ptr<Framebuffer> targetFBO;
        for (auto& key : {"ForwardTest", "FXAA", "PostProcess"}) {
            auto it = context.Framebuffers.find(key);
            if (it != context.Framebuffers.end()) { targetFBO = it->second; break; }
        }

        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL && targetFBO) {
            // OpenGL: 直接绑最终 FBO，复用其 render pass，反正 LoadOp=Clear 但上帧已画完
            // 实际需要 LoadOp=LOAD。当前用独立 UI FBO + blit 替代方案。
            uint32_t w = context.Get<uint32_t>("ViewportWidth", 1280);
            uint32_t h = context.Get<uint32_t>("ViewportHeight", 720);
            if (!m_UIFBO || m_UIFBOWidth != w || m_UIFBOHeight != h) {
                FramebufferSpecification uiSpec;
                uiSpec.Width  = w;
                uiSpec.Height = h;
                uiSpec.Attachments = { FramebufferTextureFormat::RGBA8 };
                uiSpec.Attachments.Attachments[0].LoadOp = AttachmentLoadOp::Load;
                m_UIFBO = Framebuffer::Create(uiSpec);
                m_UIFBOWidth  = w;
                m_UIFBOHeight = h;
            }
            // Blit 最终场景 → UI FBO，然后在 UI FBO 上画 UI
            GLuint srcFBO = (GLuint)(uintptr_t)targetFBO->GetRendererID();
            GLuint dstFBO = (GLuint)(uintptr_t)m_UIFBO->GetRendererID();
            GLint prevRead = 0, prevDraw = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, (GLint*)&prevRead);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, (GLint*)&prevDraw);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
            glBlitFramebuffer(0, 0, (GLint)w, (GLint)h, 0, 0, (GLint)w, (GLint)h,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);

            cmd.BeginRenderPass(m_UIFBO, false, glm::vec4(0.0f));
            Flush(cmd);
            cmd.EndRenderPass();

            // Blit UI FBO 回到最终场景 FBO
            glBindFramebuffer(GL_READ_FRAMEBUFFER, dstFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, srcFBO);
            glBlitFramebuffer(0, 0, (GLint)w, (GLint)h, 0, 0, (GLint)w, (GLint)h,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
        } else {
            // Vulkan: 对最终场景 FBO 创建 LOAD_OP_LOAD 的 render pass，UI 直接画在场景上
            auto vkFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(targetFBO);
            if (!vkFBO) return;
            VkRenderPass  loadRP = vkFBO->EnsureLoadRenderPass();
            VkFramebuffer loadFB = vkFBO->EnsureLoadFramebuffer();
            if (!loadRP || !loadFB) return;

            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            if (!vkCtx) return;
            VkCommandBuffer cb = vkCtx->GetCurrentCommandBuffer();

            uint32_t w = context.Get<uint32_t>("ViewportWidth", 1280);
            uint32_t h = context.Get<uint32_t>("ViewportHeight", 720);

            VkClearValue cv{};
            VkRenderPassBeginInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpInfo.renderPass = loadRP;
            rpInfo.framebuffer = loadFB;
            rpInfo.renderArea.extent = { w, h };
            rpInfo.clearValueCount = 1;
            rpInfo.pClearValues = &cv;
            vkCmdBeginRenderPass(cb, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport vp{};
            vp.x = 0; vp.y = (float)h;
            vp.width = (float)w; vp.height = -(float)h;
            vp.minDepth = 0; vp.maxDepth = 1;
            vkCmdSetViewport(cb, 0, 1, &vp);
            VkRect2D scissor{ {0, 0}, {w, h} };
            vkCmdSetScissor(cb, 0, 1, &scissor);

            // 绑定 UI pipeline（Alpha blend，无深度）
            if (m_UIPipeline) {
                auto vkPipeline = std::dynamic_pointer_cast<VulkanPipeline>(m_UIPipeline);
                if (vkPipeline) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      vkPipeline->GetVulkanPipeline());
                }
            }

            Flush(cmd);

            vkCmdEndRenderPass(cb);
        }
    }

}
