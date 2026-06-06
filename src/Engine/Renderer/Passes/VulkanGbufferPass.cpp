#include "ayapch.h"
#include "VulkanGbufferPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderQueue.hpp"
#include "Asset/AssetManager.hpp"

namespace Ayaya {

    void VulkanGBufferPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        FramebufferSpecification spec;
        spec.Width = width; spec.Height = height; spec.Samples = 1;
        spec.Attachments = {
            FramebufferTextureFormat::RGBA32F, FramebufferTextureFormat::RGBA16F,
            FramebufferTextureFormat::RGBA8,   FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::RGBA8,   FramebufferTextureFormat::Depth
        };
        builder.WriteTexture("GBuffer", spec);
    }

    void VulkanGBufferPass::OnAttach() {
        m_GBufferShader = Shader::Create("Deferred/gbuffer.vert", "Deferred/gbuffer.frag");
        m_PipeSpec.Shader = m_GBufferShader;
        m_PipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        m_PipeSpec.DepthTest = true;
        m_PipeSpec.DepthWrite = true;
        m_PipeSpec.Blend = false;
        m_PipeSpec.BackfaceCulling = CullMode::None;

        // Pre-create pipeline with format reference FBO (same format as RenderGraph)
        FramebufferSpecification refSpec;
        refSpec.Width = 1280; refSpec.Height = 720; refSpec.Samples = 1;
        refSpec.Attachments = {
            FramebufferTextureFormat::RGBA32F, FramebufferTextureFormat::RGBA16F,
            FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth
        };
        m_RefFBO = Framebuffer::Create(refSpec);
        m_PipeSpec.TargetFramebuffer = m_RefFBO;
        m_Pipeline = Pipeline::Create(m_PipeSpec);
    }

    void VulkanGBufferPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto fbo = context.GetFramebuffer("GBuffer");
        if (!fbo) return;

        auto* queue = context.RenderQueue;
        if (!queue || queue->Packets.empty()) return;

        cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
        cmd.BindPipeline(m_Pipeline);
        auto whiteTex = context.GetTexture("WhiteTexture");

        Entity selected = context.Get<Entity>("SelectedEntity", Entity{});
        Entity hovered  = context.Get<Entity>("HoveredEntity", Entity{});

        // State-sorting: track current material to avoid redundant binds
        uint64_t currentMaterialHash = 0xFFFFFFFFFFFFFFFF;

        for (const auto& packet : queue->Packets) {
            SortKey key; key.Value = packet.SortKey;
            uint8_t bucket = static_cast<uint8_t>(key.Bits.BucketID);

            // GBuffer only handles Opaque + Masked
            if (bucket > 1) continue;  // skip Translucent, Skybox, Overlay

            // ---- Fallback white textures (reset per draw) ----
            if (whiteTex) {
                cmd.BindTexture2D(m_Pipeline, "u_AlbedoMap",    1, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_MetallicMap",  2, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_RoughnessMap", 3, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_AOMap",        4, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_NormalMap",    5, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_AlphaMap",     6, whiteTex);
            }

            GBufferPushConstants pc{};
            pc.Transform = packet.Transform;
            pc.ReceiveShadows = packet.ReceiveShadows ? 1.0f : 0.0f;
            pc.Albedo = glm::vec3(1.0f);
            pc.Metallic = 0.0f;
            pc.Roughness = 0.5f;
            pc.AO = 1.0f;
            pc.AlphaMultiplier = 1.0f;
            pc.IsSelected = 0;
            if (packet.MaterialAsset) {
                pc.AlphaCutoff = packet.MaterialAsset->GetAlphaCutoff();
                pc.BlendMode   = static_cast<int>(packet.MaterialAsset->GetBlendMode());
            }

            // Read material properties every draw (cheap — just reads floats/Vec3/bools)
            if (packet.MaterialAsset) {
                for (auto& prop : packet.MaterialAsset->Properties) {
                    if (prop.Type == MaterialPropertyType::Vec3 && prop.UniformName == "u_Albedo")
                        pc.Albedo = prop.Vec3Value;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Metallic")
                        pc.Metallic = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Roughness")
                        pc.Roughness = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_AO")
                        pc.AO = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Alpha")
                        pc.AlphaMultiplier = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Bool && prop.UniformName == "u_UseAlphaMap")
                        pc.UseAlphaMap = prop.BoolValue ? 1 : 0;
                    else if (prop.Type == MaterialPropertyType::Texture2D) {
                        bool hasTex = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) || (prop.RuntimeTexture != nullptr);
                        if (hasTex) {
                            if (prop.UniformName == "u_AlbedoMap")       pc.UseAlbedoMap = 1;
                            else if (prop.UniformName == "u_MetallicMap")  pc.UseMetallicMap = 1;
                            else if (prop.UniformName == "u_RoughnessMap") pc.UseRoughnessMap = 1;
                            else if (prop.UniformName == "u_AOMap")        pc.UseAOMap = 1;
                            else if (prop.UniformName == "u_NormalMap")    pc.UseNormalMap = 1;
                            else if (prop.UniformName == "u_AlphaMap")     pc.UseAlphaMap = 1;
                        }
                    }
                }
            }

            // ---- State-sorted texture binding (expensive, only on hash change) ----
            if (key.Bits.MaterialHash != currentMaterialHash) {
                currentMaterialHash = key.Bits.MaterialHash;
                if (packet.MaterialAsset) {
                    for (auto& prop : packet.MaterialAsset->Properties) {
                        if (prop.Type == MaterialPropertyType::Texture2D) {
                            bool hasTex = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) || (prop.RuntimeTexture != nullptr);
                            if (hasTex) {
                                auto tex = prop.RuntimeTexture ? prop.RuntimeTexture : AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                                if (prop.UniformName == "u_AlbedoMap")    { cmd.BindTexture2D(m_Pipeline, "u_AlbedoMap", 1, tex); pc.UseAlbedoMap = 1; }
                                else if (prop.UniformName == "u_MetallicMap")  { cmd.BindTexture2D(m_Pipeline, "u_MetallicMap", 2, tex); pc.UseMetallicMap = 1; }
                                else if (prop.UniformName == "u_RoughnessMap") { cmd.BindTexture2D(m_Pipeline, "u_RoughnessMap", 3, tex); pc.UseRoughnessMap = 1; }
                                else if (prop.UniformName == "u_AOMap")        { cmd.BindTexture2D(m_Pipeline, "u_AOMap", 4, tex); pc.UseAOMap = 1; }
                                else if (prop.UniformName == "u_NormalMap")    { cmd.BindTexture2D(m_Pipeline, "u_NormalMap", 5, tex); pc.UseNormalMap = 1; }
                                else if (prop.UniformName == "u_AlphaMap")     { cmd.BindTexture2D(m_Pipeline, "u_AlphaMap", 6, tex); pc.UseAlphaMap = 1; }
                            }
                        }
                    }
                }
            }

            cmd.PushConstantData(m_Pipeline, &pc, sizeof(GBufferPushConstants));
            cmd.DrawIndexed(packet.MeshAsset, packet.MeshAsset->GetIndexCount());
        }

        cmd.EndRenderPass();
        // RenderGraph InsertTileResolveBarrier handles all attachments
        context.Framebuffers["GBuffer"] = fbo;
    }
}
