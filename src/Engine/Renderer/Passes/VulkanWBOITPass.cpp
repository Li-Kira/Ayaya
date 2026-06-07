#include "ayapch.h"
#include "VulkanWBOITPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderQueue.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Material.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Texture.hpp"

namespace Ayaya {

    void VulkanWBOITPass::OnAttach() {
        m_GatherShader  = Shader::Create("WBOIT/wboit_gather.vert",  "WBOIT/wboit_gather.frag");
        m_ResolveShader = Shader::Create("PostProcess/postprocess.vert", "WBOIT/wboit_resolve.frag");

        // Ref FBO for pipeline creation (dual-attachment: RGBA16F + R16F)
        FramebufferSpecification gatherRef;
        gatherRef.Width = 1280; gatherRef.Height = 720; gatherRef.Samples = 1;
        gatherRef.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RG16F };
        m_GatherRefFBO = Framebuffer::Create(gatherRef);

        // Gather pipeline: dual-attachment with per-attachment blend
        m_GatherSpec.Shader = m_GatherShader;
        m_GatherSpec.TargetFramebuffer = m_GatherRefFBO;
        m_GatherSpec.Layout = {
            {ShaderDataType::Float3, "a_Position"},
            {ShaderDataType::Float3, "a_Normal"},
            {ShaderDataType::Float2, "a_TexCoord"},
            {ShaderDataType::Float3, "a_Tangent"}   // matches Mesh VBO stride
        };
        m_GatherSpec.DepthTest = true;
        m_GatherSpec.DepthWrite = false;   // transparent: never write depth
        m_GatherSpec.Blend = true;
        m_GatherSpec.BackfaceCulling = CullMode::None;
        // Attachment 0 (Accumulation): additive
        // Attachment 1 (Revealage):    WBOIT multiply-attenuation
        m_GatherSpec.PerAttachmentBlend = { BlendModeType::Additive, BlendModeType::WBOITRevealage };
        m_GatherPipeline = Pipeline::Create(m_GatherSpec);

        // Resolve pipeline: full-screen quad onto Lighting HDR (RGBA16F + Depth)
        FramebufferSpecification resolveRef;
        resolveRef.Width = 1280; resolveRef.Height = 720; resolveRef.Samples = 1;
        resolveRef.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        m_ResolveRefFBO = Framebuffer::Create(resolveRef);

        m_ResolveSpec.Shader = m_ResolveShader;
        m_ResolveSpec.TargetFramebuffer = m_ResolveRefFBO;  // matches Lighting format
        m_ResolveSpec.Layout = {};
        m_ResolveSpec.DepthTest = false;
        m_ResolveSpec.DepthWrite = false;
        m_ResolveSpec.Blend = true;
        m_ResolveSpec.BlendMode = BlendModeType::Alpha;
        m_ResolveSpec.BackfaceCulling = CullMode::None;
        m_ResolvePipeline = Pipeline::Create(m_ResolveSpec);
    }

    void VulkanWBOITPass::OnResize(uint32_t width, uint32_t height) {
        // No internal FBOs — all managed by RenderGraph
    }

    void VulkanWBOITPass::DeclareGatherResources(RGBuilder& builder,
                                                  uint32_t width, uint32_t height) {
        // Single combined FBO with dual color attachments for WBOIT accumulation
        // Attachment 0: WBOIT_Accumulation (RGBA16F) — additive blend
        // Attachment 1: WBOIT_Revealage    (RG16F)   — multiply-attenuation
        FramebufferSpecification gatherSpec;
        gatherSpec.Width = width; gatherSpec.Height = height; gatherSpec.Samples = 1;
        gatherSpec.Attachments = { FramebufferTextureFormat::RGBA16F,
                                   FramebufferTextureFormat::RG16F };
        builder.WriteTexture("WBOIT_Gather", gatherSpec);

        // Depth-test against opaque geometry (read GBuffer depth)
        builder.ReadTexture("GBuffer");
    }

    void VulkanWBOITPass::DeclareResolveResources(RGBuilder& builder,
                                                   uint32_t width, uint32_t height) {
        builder.ReadTexture("WBOIT_Gather");

        // Read-write onto Lighting HDR (LOAD composite)
        FramebufferSpecification hdrSpec;
        hdrSpec.Width = width; hdrSpec.Height = height; hdrSpec.Samples = 1;
        hdrSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        builder.ReadWriteTexture("Lighting", hdrSpec);
    }

    void VulkanWBOITPass::ExecuteGather(RenderContext& context,
                                         RenderCommandBuffer& cmd) {
        auto gatherFBO = context.GetFramebuffer("WBOIT_Gather");
        if (!gatherFBO) return;

        auto* queue = context.RenderQueue;

        // Check if any translucent packets exist — skip the pass entirely if not
        bool hasTranslucent = false;
        if (queue) {
            for (const auto& p : queue->Packets) {
                SortKey k; k.Value = p.SortKey;
                if (k.Bits.BucketID == static_cast<uint64_t>(RenderBucket::Translucent))
                { hasTranslucent = true; break; }
            }
        }
        if (!hasTranslucent) return;

        auto whiteTex = context.GetTexture("WhiteTexture");

        // Per-attachment clear: [0]=black(0,0,0,0) [1]=revealage-start(1.0)
        cmd.SetPerAttachmentClearColors({
            glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
            glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)  // R16F: .r = 1.0
        });
        cmd.BeginRenderPass(gatherFBO, true, glm::vec4(0.0f));
        cmd.BindPipeline(m_GatherPipeline);

        // Bind IBL textures (shared across all translucent objects)
        auto irrMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap");
        auto preMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
        auto brdfLUT = context.GetTexture("BRDFLUT");
        auto whiteTex2 = context.GetTexture("WhiteTexture");
        if (irrMap)  cmd.BindTextureCube(m_GatherPipeline, "u_IrradianceMap",  3, irrMap);
        if (preMap)  cmd.BindTextureCube(m_GatherPipeline, "u_PrefilteredMap", 4, preMap);
        if (brdfLUT) cmd.BindTexture2D(m_GatherPipeline, "u_BRDFLUT", 5, brdfLUT);
        else if (whiteTex2) cmd.BindTexture2D(m_GatherPipeline, "u_BRDFLUT", 5, whiteTex2);

        // State-sorting by material
        uint64_t currentMaterialHash = 0xFFFFFFFFFFFFFFFF;

        for (const auto& packet : queue->Packets) {
            SortKey key; key.Value = packet.SortKey;
            uint8_t bucket = static_cast<uint8_t>(key.Bits.BucketID);

            // Only handle Translucent bucket (bucket 3)
            if (bucket != 3) continue;

            WBOITGatherPushConstants pc{};
            pc.Transform = packet.Transform;
            pc.Albedo = glm::vec4(1.0f);
            pc.Metallic = 0.0f;
            pc.Roughness = 0.5f;
            pc.AO = 1.0f;
            pc.Alpha = 0.5f;

            // Read material properties (always, per-packet)
            // Scalars AND Use*Map flags must be set for every packet,
            // not just on material change — otherwise objects sharing
            // a material get Use*Map=0 and sample nothing.
            if (packet.MaterialAsset) {
                for (auto& prop : packet.MaterialAsset->Properties) {
                    // Scalars
                    if (prop.Type == MaterialPropertyType::Vec3 && prop.UniformName == "u_Albedo")
                        pc.Albedo = glm::vec4(prop.Vec3Value, 1.0f);
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Metallic")
                        pc.Metallic = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Roughness")
                        pc.Roughness = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_AO")
                        pc.AO = prop.FloatValue;
                    else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Alpha")
                        pc.Alpha = prop.FloatValue;
                    // Texture Use*Map flags (always, per-packet)
                    else if (prop.Type == MaterialPropertyType::Texture2D) {
                        bool hasTex = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle))
                                   || (prop.RuntimeTexture != nullptr);
                        if (hasTex) {
                            if (prop.UniformName == "u_AlbedoMap")        pc.UseAlbedoMap = 1;
                            else if (prop.UniformName == "u_MetallicMap")  pc.UseMetallicMap = 1;
                            else if (prop.UniformName == "u_RoughnessMap") pc.UseRoughnessMap = 1;
                        }
                    }
                }
            }

            // Texture binding: only rebind on material change.
            // White fallbacks go first so slots without a texture
            // don't leak the previous material's binding.
            if (key.Bits.MaterialHash != currentMaterialHash) {
                currentMaterialHash = key.Bits.MaterialHash;

                // Reset to white fallbacks
                if (whiteTex) {
                    cmd.BindTexture2D(m_GatherPipeline, "u_AlbedoMap",    0, whiteTex);
                    cmd.BindTexture2D(m_GatherPipeline, "u_MetallicMap",  1, whiteTex);
                    cmd.BindTexture2D(m_GatherPipeline, "u_RoughnessMap", 2, whiteTex);
                }

                if (packet.MaterialAsset) {
                    for (auto& prop : packet.MaterialAsset->Properties) {
                        if (prop.Type == MaterialPropertyType::Texture2D) {
                            bool hasTex = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle))
                                       || (prop.RuntimeTexture != nullptr);
                            if (hasTex) {
                                std::shared_ptr<Texture2D> tex = prop.RuntimeTexture
                                    ? prop.RuntimeTexture
                                    : AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                                if (prop.UniformName == "u_AlbedoMap")    cmd.BindTexture2D(m_GatherPipeline, "u_AlbedoMap", 0, tex);
                                else if (prop.UniformName == "u_MetallicMap")  cmd.BindTexture2D(m_GatherPipeline, "u_MetallicMap", 1, tex);
                                else if (prop.UniformName == "u_RoughnessMap") cmd.BindTexture2D(m_GatherPipeline, "u_RoughnessMap", 2, tex);
                            }
                        }
                    }
                }
            }

            cmd.PushConstantData(m_GatherPipeline, &pc, sizeof(WBOITGatherPushConstants));
            cmd.DrawIndexed(packet.MeshAsset, packet.MeshAsset->GetIndexCount());
        }

        cmd.EndRenderPass();
    }

    void VulkanWBOITPass::ExecuteResolve(RenderContext& context,
                                          RenderCommandBuffer& cmd) {
        auto hdrFBO    = context.GetFramebuffer("Lighting");
        auto gatherFBO = context.GetFramebuffer("WBOIT_Gather");
        if (!hdrFBO || !gatherFBO) return;

        // Skip if no translucent packets were rendered
        bool hasTranslucent = false;
        auto* queue = context.RenderQueue;
        if (queue) {
            for (const auto& p : queue->Packets) {
                SortKey k; k.Value = p.SortKey;
                if (k.Bits.BucketID == static_cast<uint64_t>(RenderBucket::Translucent))
                { hasTranslucent = true; break; }
            }
        }
        if (!hasTranslucent) return;

        // Composite onto Lighting HDR with LOAD (preserves existing deferred content).
        // Output is raw HDR — PostProcess handles exposure + tone-mapping uniformly
        // for both opaque deferred and transparent WBOIT layers.
        cmd.BeginRenderPass(hdrFBO, false);  // LOAD
        cmd.BindPipeline(m_ResolvePipeline);
        cmd.BindTexture2D(m_ResolvePipeline, "u_Accumulation", 0, gatherFBO, 0);
        cmd.BindTexture2D(m_ResolvePipeline, "u_Revealage",    1, gatherFBO, 1);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();

        context.Framebuffers["Lighting"] = hdrFBO;
    }

}
