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
#include "Platform/Vulkan/VulkanBuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    static UIRenderData s_Data;

    UIPass::UIPass() { m_PassName = "UI Pass"; }
    UIPass::~UIPass() {}

    void UIPass::OnAttach() {
        Init();
        if (m_UIPipeline) return;

        m_PipeSpec.Shader = s_Data.UIShader;
        m_PipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position"  },
            { ShaderDataType::Float4, "a_Color"     },
            { ShaderDataType::Float2, "a_TexCoord"  },
            { ShaderDataType::Float,  "a_TexIndex"  },
        };
        m_PipeSpec.DepthTest = false;
        m_PipeSpec.DepthWrite = false;
        m_PipeSpec.Blend = true;
        m_PipeSpec.BackfaceCulling = CullMode::None;
        m_PipeSpec.NoGlobalUBOs = true;
        m_PipeSpec.UseBindlessTextures = (RendererAPI::GetAPI() == RendererAPI::API::Vulkan);
    }

    void UIPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        FramebufferSpecification spec;
        spec.Width  = width;
        spec.Height = height;
        spec.Attachments = { FramebufferTextureFormat::RGBA8 };
        builder.WriteTexture("UI_Output", spec);
    }

    void UIPass::Shutdown() {
        delete[] s_Data.QuadVertexBufferBase;
        s_Data.QuadVertexBufferBase = nullptr;
        s_Data.QuadIB.reset();
        s_Data.UIShader.reset();
        s_Data.WhiteTexture.reset();
        for (auto& tex : s_Data.TextureSlots) tex.reset();
        s_Data.ActiveTexture.reset();
        s_Data.QuadIndexCount = 0;
        AYAYA_CORE_INFO("UIPass: Shutdown complete");
    }

    void UIPass::Init() {
        if (m_Initialized || s_Data.WhiteTexture) return;

        s_Data.QuadVertexBufferBase = new UIVertex[UIRenderData::MaxVertices];
        s_Data.UIShader = Shader::Create("UI/ui.vert", "UI/ui.frag");

        auto* quadIndices = new uint32_t[UIRenderData::MaxIndices];
        uint32_t offset = 0;
        for (uint32_t i = 0; i < UIRenderData::MaxIndices; i += 6) {
            quadIndices[i + 0] = offset + 0; quadIndices[i + 1] = offset + 1; quadIndices[i + 2] = offset + 2;
            quadIndices[i + 3] = offset + 3; quadIndices[i + 4] = offset + 4; quadIndices[i + 5] = offset + 5;
            offset += 6;
        }
        s_Data.QuadIB = IndexBuffer::Create(quadIndices, UIRenderData::MaxIndices);
        delete[] quadIndices;

        s_Data.WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whitePixel = 0xFFFFFFFF;
        s_Data.WhiteTexture->SetData(&whitePixel, sizeof(whitePixel));
        s_Data.TextureSlots[0] = s_Data.WhiteTexture;

        s_Data.QuadVertexPositions[0] = { 0.0f, 0.0f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[1] = { 1.0f, 0.0f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[2] = { 1.0f, 1.0f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[3] = { 0.0f, 1.0f, 0.0f, 1.0f };

        m_Initialized = true;
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

        float orthoW = (float)m_ViewportWidth;
        float orthoH = (float)m_ViewportHeight;

        // 双端统一: BeginRenderPass 的负高度 Viewport 已处理 Y 轴差异
        glm::mat4 ortho = glm::ortho(0.0f, orthoW, orthoH, 0.0f, -1.0f, 1.0f);

        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            cmd.PushConstantData(m_UIPipeline, &ortho, sizeof(glm::mat4));
        } else {
            s_Data.UIShader->Bind();
            s_Data.UIShader->SetMat4("u_ViewProjection", ortho);
        }

        auto vb = VertexBuffer::Create((float*)s_Data.QuadVertexBufferBase, dataSize);
        vb->SetLayout({
            { ShaderDataType::Float3, "a_Position"  },
            { ShaderDataType::Float4, "a_Color"     },
            { ShaderDataType::Float2, "a_TexCoord"  },
            { ShaderDataType::Float,  "a_TexIndex"  },
        });

        auto va = VertexArray::Create();
        va->AddVertexBuffer(vb);
        va->SetIndexBuffer(s_Data.QuadIB);

        // Triple-buffer GC: 压入当前帧回收队列，3 帧后 GPU 执行完毕再释放
        uint32_t frameIndex = 0;
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
            if (vkCtx) frameIndex = vkCtx->GetCurrentFrameIndex() % 3;
        }
        m_VBGCTrash[frameIndex].push_back(vb);
        m_VAGCTrash[frameIndex].push_back(va);

        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            auto vkVB = std::dynamic_pointer_cast<VulkanVertexBuffer>(vb);
            if (vkVB) {
                auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
                if (vkCtx) {
                    VkBuffer vbs[] = { vkVB->GetVulkanBuffer() };
                    VkDeviceSize off[] = { 0 };
                    vkCmdBindVertexBuffers(vkCtx->GetCurrentCommandBuffer(), 0, 1, vbs, off);
                }
            }
            cmd.DrawArrays(vertexCount);
        } else {
            s_Data.UIShader->Bind();
            auto& tex = s_Data.ActiveTexture ? s_Data.ActiveTexture : s_Data.WhiteTexture;
            tex->Bind(0);
            va->Bind();
            cmd.DrawIndexed(va, s_Data.QuadIndexCount);
        }
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
        bool bindless = (RendererAPI::GetAPI() == RendererAPI::API::Vulkan);

        if (!bindless) {
            // OpenGL 传统路径：按纹理切换 Flush 批处理
            if (texture != s_Data.ActiveTexture) {
                if (!(texture && s_Data.ActiveTexture &&
                      texture->GetRendererID() == s_Data.ActiveTexture->GetRendererID())) {
                    Flush(cmd);
                    StartBatch();
                    s_Data.ActiveTexture = texture;
                }
            }
        }

        if (s_Data.QuadIndexCount >= UIRenderData::MaxIndices) {
            NextBatch(cmd);
            s_Data.ActiveTexture = texture;
        }

        bool dataFlipped = texture && texture->IsDataFlipped();
        bool flipV = bindless && dataFlipped;

        float texIndex = 0.0f;
        if (bindless) {
            texIndex = (float)(texture ? texture->GetBindlessIndex() : s_Data.WhiteTexture->GetBindlessIndex());
            if (texIndex == 0.0f) texIndex = (float)s_Data.WhiteTexture->GetBindlessIndex();
        }

        constexpr glm::vec2 tc[6] = { {0,0}, {1,0}, {1,1}, {1,1}, {0,1}, {0,0} };
        int order[6] = {0, 1, 2, 2, 3, 0};
        for (int j = 0; j < 6; j++) {
            int i = order[j];
            glm::vec4 pos = renderTransform * s_Data.QuadVertexPositions[i];
            s_Data.QuadVertexBufferPtr->px = pos.x; s_Data.QuadVertexBufferPtr->py = pos.y; s_Data.QuadVertexBufferPtr->pz = 0.0f;
            s_Data.QuadVertexBufferPtr->cr = color.r; s_Data.QuadVertexBufferPtr->cg = color.g;
            s_Data.QuadVertexBufferPtr->cb = color.b; s_Data.QuadVertexBufferPtr->ca = color.a;
            s_Data.QuadVertexBufferPtr->tu = tc[j].x;
            s_Data.QuadVertexBufferPtr->tv = flipV ? (1.0f - tc[j].y) : tc[j].y;
            s_Data.QuadVertexBufferPtr->TexIndex = texIndex;
            s_Data.QuadVertexBufferPtr++;
        }
        s_Data.QuadIndexCount += 6;
    }

    void UIPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        Init();
        auto* scene = context.ActiveScene.get();
        if (!scene) return;

        uint32_t frameIndex = 0;
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
            if (vkCtx) frameIndex = vkCtx->GetCurrentFrameIndex() % 3;
        }
        m_VBGCTrash[frameIndex].clear();
        m_VAGCTrash[frameIndex].clear();

        uint32_t width  = context.Get<uint32_t>("ViewportWidth", 1280);
        uint32_t height = context.Get<uint32_t>("ViewportHeight", 720);
        if (width == 0 || height == 0) return;

        m_ViewportWidth  = width;
        m_ViewportHeight = height;

        UILayoutSystem::Update(*scene, width, height);

        auto uiFBO = context.GetFramebuffer("UI_Output");
        if (!uiFBO) { AYAYA_CORE_WARN("[UIPass] UI_Output FBO not found!"); return; }

        if (!m_UIPipeline) {
            m_PipeSpec.TargetFramebuffer = uiFBO;
            m_UIPipeline = Pipeline::Create(m_PipeSpec);
        }

        cmd.BeginRenderPass(uiFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        cmd.BindPipeline(m_UIPipeline);

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
                        if (img.TextureHandle != 0) {
                            tex = AssetManager::GetAsset<Texture2D>(img.TextureHandle);
                            bool gpuReady = (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
                                ? (tex && tex->GetBindlessIndex() != 0)
                                : (tex && tex->GetRendererID() != 0);
                            if (!gpuReady) tex = nullptr;
                        }
                        DrawUIElement(cmd, rt.RenderTransform, img.Color, tex, (int)entt::to_integral(childID));
                    }
                    if (child.HasComponent<RelationshipComponent>()) collectUI(child);
                }
            };
            collectUI(canvas);
        }

        if (s_Data.QuadIndexCount > 0) Flush(cmd);
        cmd.EndRenderPass();

        context.Framebuffers["UI"] = context.GetFramebuffer("UI_Output");
    }

}
