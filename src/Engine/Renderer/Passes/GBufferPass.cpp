#include "ayapch.h"
#include "GBufferPass.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Frustum.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"

#include <algorithm>

namespace Ayaya {

    GBufferPass::GBufferPass() {
        m_PassName = "G-Buffer Geometry Pass";
    }

    void GBufferPass::OnAttach() {
        m_GBufferShader = Shader::Create("assets/Editor/shaders/Deferred/gbuffer.vert", "assets/Editor/shaders/Deferred/gbuffer.frag");
        m_FallbackShader = Shader::Create("assets/Editor/shaders/Fallback/fallback.vert", "assets/Editor/shaders/Fallback/fallback.frag");

        m_FallbackMaterial = std::make_shared<Material>();
        m_FallbackMaterial->Name = "Error Fallback";

        m_GBufferShader->BindUniformBlock("Camera", 0);
        m_FallbackShader->BindUniformBlock("Camera", 0);

        FramebufferSpecification geoSpec;
        geoSpec.Samples = 1;
        geoSpec.Width = 1280; geoSpec.Height = 720;
        geoSpec.Attachments = { 
            FramebufferTextureFormat::RGBA32F, // Position
            FramebufferTextureFormat::RGBA16F, // Normal
            FramebufferTextureFormat::RGBA8,   // Albedo
            FramebufferTextureFormat::RGBA8,   // PBR (R:AO, G:Rough, B:Metal)
            FramebufferTextureFormat::Depth    // Depth
        };
        m_GeometryFBO = Framebuffer::Create(geoSpec);
    }

    void GBufferPass::OnResize(uint32_t width, uint32_t height) {
        m_GeometryFBO->Resize(width, height);
    }

    void GBufferPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        // 1. 视锥体剔除与数据收集
        m_OpaqueDrawList.clear();
        Frustum frustum(context.ProjectionMatrix * context.ViewMatrix);
        
        auto meshView = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshView) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;
            
            auto& tc = entity.GetComponent<TransformComponent>();
            auto& meshComp = entity.GetComponent<MeshRendererComponent>();

            if (!meshComp.ModelAsset) continue;

            glm::mat4 transform = tc.GetTransform();
            
            // 剔除判定
            bool isInside = false;
            if (meshComp.ModelAsset->GetMeshes().size() > 0) {
                const auto& aabb = meshComp.ModelAsset->GetMeshes()[0]->GetAABB();
                isInside = frustum.IsBoxVisible(aabb, transform);
            }

            if (isInside) {
                RenderCommandData drawCmd;
                drawCmd.Transform = transform;
                drawCmd.TargetEntity = entity;
                drawCmd.CastShadows = meshComp.CastShadows;
                drawCmd.ReceiveShadows = meshComp.ReceiveShadows;

                for (size_t i = 0; i < meshComp.ModelAsset->GetMeshes().size(); i++) {
                    drawCmd.MeshAsset = meshComp.ModelAsset->GetMeshes()[i];
                    drawCmd.MaterialAsset = m_FallbackMaterial;
                    drawCmd.ShaderAsset = m_GBufferShader;
                    m_OpaqueDrawList.push_back(drawCmd);
                }
            }
        }

        // 2. 状态准备
        m_GeometryFBO->Bind();
        cmd.SetViewport(0, 0, m_GeometryFBO->GetSpecification().Width, m_GeometryFBO->GetSpecification().Height);
        cmd.SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        cmd.Clear();
        cmd.SetDepthTest(true);

        auto whiteTexture = context.GetTexture("WhiteTexture");

        // 3. 几何渲染
        for (const auto& drawCmd : m_OpaqueDrawList) {
            auto currentShader = drawCmd.ShaderAsset ? drawCmd.ShaderAsset : m_GBufferShader;
            currentShader->Bind();
            context.Stats.ShaderBinds++;

            if (drawCmd.MaterialAsset) {
                uint32_t textureSlot = 0;
                for (const auto& prop : drawCmd.MaterialAsset->Properties) {
                        switch (prop.Type) {
                            case MaterialPropertyType::Float: currentShader->SetFloat(prop.UniformName, prop.FloatValue); break;
                            case MaterialPropertyType::Vec2: currentShader->SetFloat2(prop.UniformName, prop.Vec2Value); break;
                            case MaterialPropertyType::Vec3: currentShader->SetFloat3(prop.UniformName, prop.Vec3Value); break;
                            case MaterialPropertyType::Vec4: currentShader->SetFloat4(prop.UniformName, prop.Vec4Value); break;
                            case MaterialPropertyType::Bool: currentShader->SetBool(prop.UniformName, prop.BoolValue); break;
                            case MaterialPropertyType::Texture2D:
                                currentShader->SetInt(prop.UniformName, textureSlot);
                                if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                    auto tex = AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                                    // 引擎内部的 Texture 抽象依然可以直接 Bind，暂不破坏
                                    tex->Bind(textureSlot);
                                } else {
                                    if (whiteTexture) whiteTexture->Bind(textureSlot); 
                                }
                                textureSlot++;
                                break;
                            default: break;
                        }
                }
            }
            currentShader->SetFloat("u_ReceiveShadows", drawCmd.ReceiveShadows ? 1.0f : 0.0f);
            
            // 【核心替换】：告别 Renderer::Submit，直接下令绘制！
            currentShader->SetMat4("u_Transform", drawCmd.Transform);
            
            std::string tag = drawCmd.TargetEntity.GetComponent<TagComponent>().Tag;
            uint32_t tris = drawCmd.MeshAsset->GetIndexCount() / 3;

            if (context.RecordAndCheckDrawCall("G-Buffer Pass", tag, "GBuffer Shader", tris)) {
                // 指挥权完全移交给 CommandBuffer
                cmd.DrawIndexed(drawCmd.MeshAsset->GetVertexArray(), drawCmd.MeshAsset->GetIndexCount());
            }
        }

        m_GeometryFBO->Unbind();

        // 4. 将 G-Buffer 产物贴在黑板上，供下个 Pass 读取！
        context.Set("GBuffer_Position", m_GeometryFBO->GetColorAttachmentRendererID(0));
        context.Set("GBuffer_Normal", m_GeometryFBO->GetColorAttachmentRendererID(1));
        context.Set("GBuffer_Albedo", m_GeometryFBO->GetColorAttachmentRendererID(2));
        context.Set("GBuffer_PBR", m_GeometryFBO->GetColorAttachmentRendererID(3));
        context.Framebuffers["Geometry"] = m_GeometryFBO; 
    }
}