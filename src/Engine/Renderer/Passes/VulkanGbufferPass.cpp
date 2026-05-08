#include "ayapch.h"
#include "VulkanGbufferPass.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Frustum.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"

#include <algorithm>
#include <glm/glm.hpp>

namespace Ayaya {

    VulkanGBufferPass::VulkanGBufferPass() {
        m_PassName = "G-Buffer Geometry Pass";
    }

    void VulkanGBufferPass::OnAttach() {
        m_GBufferShader = Shader::Create("Deferred/gbuffer.vert", "Deferred/gbuffer.frag");
        m_FallbackShader = Shader::Create("Fallback/fallback.vert", "Fallback/fallback.frag");

        m_FallbackMaterial = std::make_shared<Material>();
        m_FallbackMaterial->Name = "Error Fallback";

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
            FramebufferTextureFormat::RGBA8,   // 3: PBR (Metallic, Roughness, AO)
            FramebufferTextureFormat::RGBA8,   // 4: CustomData (ReceiveShadows)
            FramebufferTextureFormat::Depth    // 5: Depth
        };
        m_GeometryFBO = Framebuffer::Create(geoSpec);

        // ==========================================
        // 2. 烘焙几何管线图纸 (PSO)
        // ==========================================
       PipelineSpecification gbufferPipeSpec;
        gbufferPipeSpec.Shader = m_GBufferShader;
        gbufferPipeSpec.TargetFramebuffer = m_GeometryFBO;
        
        // ==========================================
        // 【核心修复 1】：告诉 Vulkan 顶点数据的排列格式！
        // 加上这段代码，扭曲的三角形立马变成完美的模型！
        // ==========================================
        gbufferPipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" } // 加上这行，怪物瞬间变回模型！
        };

        gbufferPipeSpec.DepthTest = true;
        gbufferPipeSpec.DepthWrite = true;
        gbufferPipeSpec.Blend = false; 
        gbufferPipeSpec.BackfaceCulling = CullMode::None; // 禁用背面剔除以匹配前向渲染路径

        PipelineSpecification fallbackPipeSpec = gbufferPipeSpec;
        fallbackPipeSpec.Shader = m_FallbackShader;

        m_GBufferPipeline = Pipeline::Create(gbufferPipeSpec);
        m_FallbackPipeline = Pipeline::Create(fallbackPipeSpec);
    }

    void VulkanGBufferPass::OnResize(uint32_t width, uint32_t height) {
        m_GeometryFBO->Resize(width, height);
    }

    void VulkanGBufferPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");
        if (width == 0 || height == 0) return;

        if (m_GeometryFBO->GetSpecification().Width != width || m_GeometryFBO->GetSpecification().Height != height) {
            OnResize(width, height);
        }

        m_OpaqueDrawList.clear();

        // ==========================================
        // 1. 收集绘制指令
        // ==========================================
        auto view = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : view) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;

            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
            if (!model) continue;

            std::shared_ptr<Material> activeMaterial = m_FallbackMaterial;
            std::shared_ptr<Pipeline> activePipeline = m_FallbackPipeline;

            auto material = AssetManager::GetAsset<Material>(meshComp.MaterialHandle);
            if (material) {
                activeMaterial = material;
                activePipeline = m_GBufferPipeline;
            }

            VulkanGBufferCommandData drawCmd;
            drawCmd.Transform = entity.GetWorldTransform();
            drawCmd.MaterialAsset = activeMaterial;
            drawCmd.PipelineAsset = activePipeline;
            drawCmd.TargetEntity = entity;
            drawCmd.CastShadows = meshComp.CastShadows;
            drawCmd.ReceiveShadows = meshComp.ReceiveShadows;

            for (auto& mesh : model->GetMeshes()) {
                drawCmd.MeshAsset = mesh;
                m_OpaqueDrawList.push_back(drawCmd);
            }
        }

        // ==========================================
        // 2. 视锥体剔除 & 排序 (提升 Early-Z 效率)
        // ==========================================
        glm::vec3 cameraPos = context.Get<glm::vec3>("CameraPosition", glm::vec3(0.0f));
        std::sort(m_OpaqueDrawList.begin(), m_OpaqueDrawList.end(), [&cameraPos](const VulkanGBufferCommandData& a, const VulkanGBufferCommandData& b) {
            float distA = glm::distance2(glm::vec3(a.Transform[3]), cameraPos);
            float distB = glm::distance2(glm::vec3(b.Transform[3]), cameraPos);
            return distA < distB; 
        });

        // ==========================================
        // 3. 执行 G-Buffer 渲染
        // ==========================================
        cmd.BeginRenderPass(m_GeometryFBO, true, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        std::shared_ptr<Pipeline> currentPipeline = nullptr;
        std::shared_ptr<Material> currentMaterial = nullptr;

        for (auto& drawCmd : m_OpaqueDrawList) {
            // 状态机优化：按需切换 Pipeline
            if (currentPipeline != drawCmd.PipelineAsset) {
                currentPipeline = drawCmd.PipelineAsset;
                cmd.BindPipeline(currentPipeline);
                context.Stats.ShaderBinds++;
            }

            // 状态机优化：按需切换 Material
            if (currentMaterial != drawCmd.MaterialAsset) {
                currentMaterial = drawCmd.MaterialAsset;
            }

            // ==========================================
            // 打包推送常量 (Push Constants)
            // ==========================================
            if (currentPipeline == m_GBufferPipeline) {
                GBufferPushConstants constants{};
                constants.Transform = drawCmd.Transform;
                constants.ReceiveShadows = drawCmd.ReceiveShadows ? 1.0f : 0.0f;
                
                // 默认参数
                constants.Albedo = glm::vec3(1.0f);
                constants.Metallic = 0.0f;
                constants.Roughness = 0.5f;
                constants.AO = 1.0f;
                constants.UseAlbedoMap = 0;
                constants.UseMetallicMap = 0;
                constants.UseRoughnessMap = 0;
                constants.UseAOMap = 0;
                constants.UseNormalMap = 0;

                // ==========================================
                // 【核心修复】：为所有的贴图槽位提供“垫背”贴图！
                // 防止 Vulkan 报 08114 槽位未更新的错误
                // ==========================================
                auto whiteTex = context.GetTexture("WhiteTexture");
                if (whiteTex) {
                    cmd.BindTexture2D(currentPipeline, "u_AlbedoMap", 1, whiteTex);
                    cmd.BindTexture2D(currentPipeline, "u_MetallicMap", 2, whiteTex);
                    cmd.BindTexture2D(currentPipeline, "u_RoughnessMap", 3, whiteTex);
                    cmd.BindTexture2D(currentPipeline, "u_AOMap", 4, whiteTex);
                    cmd.BindTexture2D(currentPipeline, "u_NormalMap", 5, whiteTex);
                }

                // 【修复2】：通过遍历 Properties 数组获取材质数据！
                if (currentMaterial) {
                    for (const auto& prop : currentMaterial->Properties) {
                        if (prop.Type == MaterialPropertyType::Vec3 && prop.UniformName == "u_Albedo") 
                            constants.Albedo = prop.Vec3Value;
                        else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Metallic") 
                            constants.Metallic = prop.FloatValue;
                        else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_Roughness") 
                            constants.Roughness = prop.FloatValue;
                        else if (prop.Type == MaterialPropertyType::Float && prop.UniformName == "u_AO") 
                            constants.AO = prop.FloatValue;
                        else if (prop.Type == MaterialPropertyType::Texture2D) {
                            
                            // 检测是否有合法的资产句柄，或是动态生成的运行时贴图
                            bool hasValidTex = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) || (prop.RuntimeTexture != nullptr);
                            
                            if (hasValidTex) {
                                auto tex = prop.RuntimeTexture ? prop.RuntimeTexture : AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                                
                                if (prop.UniformName == "u_AlbedoMap") {
                                    cmd.BindTexture2D(currentPipeline, "u_AlbedoMap", 1, tex);
                                    constants.UseAlbedoMap = 1;
                                } else if (prop.UniformName == "u_MetallicMap") {
                                    cmd.BindTexture2D(currentPipeline, "u_MetallicMap", 2, tex);
                                    constants.UseMetallicMap = 1;
                                } else if (prop.UniformName == "u_RoughnessMap") {
                                    cmd.BindTexture2D(currentPipeline, "u_RoughnessMap", 3, tex);
                                    constants.UseRoughnessMap = 1;
                                } else if (prop.UniformName == "u_AOMap") {
                                    cmd.BindTexture2D(currentPipeline, "u_AOMap", 4, tex);
                                    constants.UseAOMap = 1;
                                } else if (prop.UniformName == "u_NormalMap") {
                                    cmd.BindTexture2D(currentPipeline, "u_NormalMap", 5, tex);
                                    constants.UseNormalMap = 1;
                                }
                            }
                        }
                    }
                }

                // 一次性提交 112 字节的数据块
                cmd.PushConstantData(currentPipeline, &constants, sizeof(GBufferPushConstants));
                
            } else if (currentPipeline == m_FallbackPipeline) {
                // Fallback 错误材质管线只需要推入 Transform 即可 (64 bytes)
                glm::mat4 fallbackTransform = drawCmd.Transform;
                cmd.PushConstantData(currentPipeline, &fallbackTransform, sizeof(glm::mat4));
            }

            // 绘制指令
            std::string tag = drawCmd.TargetEntity.GetComponent<TagComponent>().Tag;
            uint32_t tris = drawCmd.MeshAsset->GetIndexCount() / 3;

            if (context.RecordAndCheckDrawCall("G-Buffer Pass", tag, "GBuffer Shader", tris)) {
                cmd.DrawIndexed(drawCmd.MeshAsset, drawCmd.MeshAsset->GetIndexCount());
            }
        }

        cmd.EndRenderPass();
        cmd.InsertExecutionBarrier(); // flush TBDR tile writes before LightingPass reads this FBO

        // ==========================================
        // 4. 将产物贴在黑板上，供 Lighting Pass 和 ImGui 使用
        // ==========================================
        context.Set("GBuffer_Position", m_GeometryFBO->GetColorAttachmentRendererID(0));
        context.Set("GBuffer_Normal", m_GeometryFBO->GetColorAttachmentRendererID(1));
        context.Set("GBuffer_Albedo", m_GeometryFBO->GetColorAttachmentRendererID(2));
        context.Set("GBuffer_PBR", m_GeometryFBO->GetColorAttachmentRendererID(3));
        context.Set("GBuffer_CustomData", m_GeometryFBO->GetColorAttachmentRendererID(4)); 
        
        context.Set("GBuffer_FBO", m_GeometryFBO);
        context.Framebuffers["GBuffer"] = m_GeometryFBO;
    }

}