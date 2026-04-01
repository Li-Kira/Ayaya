#include "ayapch.h"
#include "GBufferPass.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Frustum.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"

#include <algorithm>
#include <glm/glm.hpp>

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
            FramebufferTextureFormat::RGBA32F, // 0: Position
            FramebufferTextureFormat::RGBA16F, // 1: Normal
            FramebufferTextureFormat::RGBA8,   // 2: Albedo
            FramebufferTextureFormat::RGBA8,   // 3: PBR (Metal, Rough, AO)
            FramebufferTextureFormat::RGBA8,   // 4: 【新增】CustomData (R:接收阴影, G:预留ID)
            FramebufferTextureFormat::Depth    // 5: Depth
        };
        m_GeometryFBO = Framebuffer::Create(geoSpec);
    }

    void GBufferPass::OnResize(uint32_t width, uint32_t height) {
        m_GeometryFBO->Resize(width, height);
    }

    void GBufferPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        m_GeometryFBO->Bind();
        cmd.SetViewport(0, 0, m_GeometryFBO->GetSpecification().Width, m_GeometryFBO->GetSpecification().Height);
        
        // 【核心修复】：Alpha 清 0 代表天空！如果设为 1，天空盒会被错误遮挡！
        cmd.SetClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        cmd.Clear();
        cmd.SetDepthTest(true);

        m_OpaqueDrawList.clear();
        glm::mat4 viewProj = context.ProjectionMatrix * context.ViewMatrix;
        Frustum cameraFrustum(viewProj);

        // 1. 收集与剔除
        auto meshGroup = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshGroup) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;

            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            if (!meshComp.ModelAsset) continue;

            glm::mat4 transform = entity.GetWorldTransform();
            bool isVisible = false;
            for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                if (cameraFrustum.IsBoxVisible(mesh->GetAABB(), transform)) { isVisible = true; break; }
            }
            if (!isVisible) continue;

            bool isFallback = (!meshComp.MaterialAsset || meshComp.MaterialAsset->Properties.empty());
            auto targetShader = isFallback ? m_FallbackShader : m_GBufferShader;
            auto targetMaterial = isFallback ? m_FallbackMaterial : meshComp.MaterialAsset;

            for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                m_OpaqueDrawList.push_back({ transform, mesh, targetMaterial, targetShader, entity, meshComp.CastShadows, meshComp.ReceiveShadows });
            }
        }

        // 2. 状态排序 (Shader -> Material -> Mesh)
        std::sort(m_OpaqueDrawList.begin(), m_OpaqueDrawList.end(), [](const auto& a, const auto& b) {
            if (a.ShaderAsset.get() != b.ShaderAsset.get()) return a.ShaderAsset.get() < b.ShaderAsset.get();
            if (a.MaterialAsset.get() != b.MaterialAsset.get()) return a.MaterialAsset.get() < b.MaterialAsset.get();
            return a.MeshAsset.get() < b.MeshAsset.get();
        });

        // 3. 批量执行
        std::shared_ptr<Shader> currentShader = nullptr;
        std::shared_ptr<Material> currentMaterial = nullptr;
        auto whiteTexture = context.GetTexture("WhiteTexture");

        // 【核心修复】：循环变量改名为 drawCmd，避免覆盖 RenderCommandBuffer& cmd
        for (const auto& drawCmd : m_OpaqueDrawList) {
            if (currentShader != drawCmd.ShaderAsset) {
                currentShader = drawCmd.ShaderAsset;
                currentShader->Bind();
                context.Stats.ShaderBinds++;
            }
            if (currentMaterial != drawCmd.MaterialAsset) {
                currentMaterial = drawCmd.MaterialAsset;
                if (currentMaterial) {
                    int textureSlot = 0; 
                    for (auto& prop : currentMaterial->Properties) {
                        switch (prop.Type) {
                            case MaterialPropertyType::Float: currentShader->SetFloat(prop.UniformName, prop.FloatValue); break;
                            case MaterialPropertyType::Vec2:  currentShader->SetFloat2(prop.UniformName, prop.Vec2Value); break;
                            case MaterialPropertyType::Vec3:  currentShader->SetFloat3(prop.UniformName, prop.Vec3Value); break;
                            case MaterialPropertyType::Vec4:  currentShader->SetFloat4(prop.UniformName, prop.Vec4Value); break;
                            case MaterialPropertyType::Bool:  currentShader->SetBool(prop.UniformName, prop.BoolValue); break;
                            case MaterialPropertyType::Texture2D:
                                currentShader->SetInt(prop.UniformName, textureSlot);
                                if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                    auto tex = AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
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
            }
            
            if (currentShader == m_GBufferShader) {
                currentShader->SetFloat("u_ReceiveShadows", drawCmd.ReceiveShadows ? 1.0f : 0.0f);
            }
            
            // 替代旧版的 Renderer::Submit
            currentShader->SetMat4("u_Transform", drawCmd.Transform);

            std::string tag = drawCmd.TargetEntity.GetComponent<TagComponent>().Tag;
            uint32_t tris = drawCmd.MeshAsset->GetIndexCount() / 3;

            if (context.RecordAndCheckDrawCall("G-Buffer Pass", tag, "GBuffer Shader", tris)) {
                cmd.DrawIndexed(drawCmd.MeshAsset->GetVertexArray(), drawCmd.MeshAsset->GetIndexCount());
            }
        }

        m_GeometryFBO->Unbind();

        // 4. 将 G-Buffer 产物贴在黑板上，供下个 Pass 读取
        context.Set("GBuffer_Position", m_GeometryFBO->GetColorAttachmentRendererID(0));
        context.Set("GBuffer_Normal", m_GeometryFBO->GetColorAttachmentRendererID(1));
        context.Set("GBuffer_Albedo", m_GeometryFBO->GetColorAttachmentRendererID(2));
        context.Set("GBuffer_PBR", m_GeometryFBO->GetColorAttachmentRendererID(3));
        context.Set("GBuffer_CustomData", m_GeometryFBO->GetColorAttachmentRendererID(4)); // 【新增】
        context.Framebuffers["Geometry"] = m_GeometryFBO;
    }
}