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
        m_GBufferShader = Shader::Create("Deferred/gbuffer.vert", "Deferred/gbuffer.frag");
        m_FallbackShader = Shader::Create("Fallback/fallback.vert", "Fallback/fallback.frag");

        m_FallbackMaterial = std::make_shared<Material>();
        m_FallbackMaterial->Name = "Error Fallback";

        m_GBufferShader->BindUniformBlock("Camera", 0);
        m_FallbackShader->BindUniformBlock("Camera", 0);

        // ==========================================
        // 1. 创建包含 CustomData 的 G-Buffer (5个颜色附件)
        // ==========================================
        FramebufferSpecification geoSpec;
        geoSpec.Samples = 1;
        geoSpec.Width = 1280; geoSpec.Height = 720;
        geoSpec.Attachments = { 
            FramebufferTextureFormat::RGBA32F, // 0: Position
            FramebufferTextureFormat::RGBA16F, // 1: Normal
            FramebufferTextureFormat::RGBA8,   // 2: Albedo
            FramebufferTextureFormat::RGBA8,   // 3: PBR (Metal, Rough, AO)
            FramebufferTextureFormat::RGBA8,   // 4: CustomData (R:接收阴影, 预留其他状态)
            FramebufferTextureFormat::Depth    // 5: Depth
        };
        m_GeometryFBO = Framebuffer::Create(geoSpec);

        // ==========================================
        // 2. 将渲染状态打包进 PSO 管线图纸
        // ==========================================
        PipelineSpecification gbufferPipeSpec;
        gbufferPipeSpec.Shader = m_GBufferShader;
        gbufferPipeSpec.TargetFramebuffer = m_GeometryFBO;
        gbufferPipeSpec.DepthTest = true;
        gbufferPipeSpec.DepthWrite = true; // 强制写入深度
        gbufferPipeSpec.BackfaceCulling = CullMode::Back; // 开启背面剔除
        m_GBufferPipeline = Pipeline::Create(gbufferPipeSpec);

        PipelineSpecification fallbackPipeSpec = gbufferPipeSpec;
        fallbackPipeSpec.Shader = m_FallbackShader;
        m_FallbackPipeline = Pipeline::Create(fallbackPipeSpec);
    }

    void GBufferPass::OnResize(uint32_t width, uint32_t height) {
        m_GeometryFBO->Resize(width, height);
    }

    void GBufferPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        // 【终极进化】：一句话开启渲染作用域，目标、视口、清空一次搞定！
        cmd.BeginRenderPass(m_GeometryFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));

        m_OpaqueDrawList.clear();
        glm::mat4 viewProj = context.ProjectionMatrix * context.ViewMatrix;
        Frustum cameraFrustum(viewProj);

        // 1. 收集与视锥体剔除
        // 1. 收集与视锥体剔除
        auto meshGroup = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshGroup) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;

            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            
            auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
            if (!model) continue;

            glm::mat4 transform = entity.GetWorldTransform();
            bool isVisible = false;
            for (auto& mesh : model->GetMeshes()) {
                if (cameraFrustum.IsBoxVisible(mesh->GetAABB(), transform)) { isVisible = true; break; }
            }
            if (!isVisible) continue;

            auto material = AssetManager::GetAsset<Material>(meshComp.MaterialHandle);
            
            bool isFallback = (!material || material->Properties.empty());
            auto targetPipeline = isFallback ? m_FallbackPipeline : m_GBufferPipeline;
            auto targetMaterial = isFallback ? m_FallbackMaterial : material;

            for (auto& mesh : model->GetMeshes()) {
                m_OpaqueDrawList.push_back({ transform, mesh, targetMaterial, targetPipeline, entity, meshComp.CastShadows, meshComp.ReceiveShadows });
            }
        }

        // 2. 状态排序 (Pipeline -> Material -> Mesh)
        std::sort(m_OpaqueDrawList.begin(), m_OpaqueDrawList.end(), [](const auto& a, const auto& b) {
            if (a.PipelineAsset.get() != b.PipelineAsset.get()) return a.PipelineAsset.get() < b.PipelineAsset.get();
            if (a.MaterialAsset.get() != b.MaterialAsset.get()) return a.MaterialAsset.get() < b.MaterialAsset.get();
            return a.MeshAsset.get() < b.MeshAsset.get();
        });

        // 3. 批量执行绘制
        std::shared_ptr<Pipeline> currentPipeline = nullptr;
        std::shared_ptr<Material> currentMaterial = nullptr;
        auto whiteTexture = context.GetTexture("WhiteTexture");

        for (const auto& drawCmd : m_OpaqueDrawList) {
            
            // 切换管线 (PSO)
            if (currentPipeline != drawCmd.PipelineAsset) {
                currentPipeline = drawCmd.PipelineAsset;
                cmd.BindPipeline(currentPipeline);
                context.Stats.ShaderBinds++;
            }
            
            // 【终极进化 1】：一键绑定材质包 (模拟 Descriptor Set)
            if (currentMaterial != drawCmd.MaterialAsset) {
                currentMaterial = drawCmd.MaterialAsset;
                if (currentMaterial) {
                    // ==========================================
                    // 【核心适配 1】：传入白模贴图对象而非 ID！
                    // ⚠️ 注意：这要求你的 `Material::Bind` 函数签名也必须修改为接收 std::shared_ptr<Texture2D>
                    // 并在其内部调用 `cmd.BindTexture2D(..., textureObject)`
                    // 如果你的 Material 类还没改，这里请暂时恢复为 `whiteTexture ? whiteTexture->GetRendererID() : 0`
                    // ==========================================
                    currentMaterial->Bind(cmd, currentPipeline, whiteTexture);
                }
            }
            
            // 【终极进化 2】：推送常量 (Push Constants)
            if (currentPipeline == m_GBufferPipeline) {
                cmd.PushConstant(currentPipeline, "u_ReceiveShadows", drawCmd.ReceiveShadows ? 1.0f : 0.0f);
            }
            cmd.PushConstant(currentPipeline, "u_Transform", drawCmd.Transform);

            // 绘制指令
            std::string tag = drawCmd.TargetEntity.GetComponent<TagComponent>().Tag;
            uint32_t tris = drawCmd.MeshAsset->GetIndexCount() / 3;

            if (context.RecordAndCheckDrawCall("G-Buffer Pass", tag, "GBuffer Shader", tris)) {
                // 完美保留你基于 VertexArray 的绘制调用
                cmd.DrawIndexed(drawCmd.MeshAsset->GetVertexArray(), drawCmd.MeshAsset->GetIndexCount());
            }
        }

        cmd.EndRenderPass();

        // ==========================================
        // 4. 将 5 张 G-Buffer 产物贴在黑板上
        // ==========================================
        // 保留你原本提取 ID 的代码，防止 EditorLayer 中的调试面板读取失败黑屏
        context.Set("GBuffer_Position", m_GeometryFBO->GetColorAttachmentRendererID(0));
        context.Set("GBuffer_Normal", m_GeometryFBO->GetColorAttachmentRendererID(1));
        context.Set("GBuffer_Albedo", m_GeometryFBO->GetColorAttachmentRendererID(2));
        context.Set("GBuffer_PBR", m_GeometryFBO->GetColorAttachmentRendererID(3));
        context.Set("GBuffer_CustomData", m_GeometryFBO->GetColorAttachmentRendererID(4)); 
        
        // ==========================================
        // 【核心适配 2】：为 Vulkan 照明阶段补充完整的 FBO 实体对象！
        // ==========================================
        context.Set("GBuffer_FBO", m_GeometryFBO); 
        context.Framebuffers["GBuffer"] = m_GeometryFBO;
        context.Framebuffers["Geometry"] = m_GeometryFBO;
    }
}