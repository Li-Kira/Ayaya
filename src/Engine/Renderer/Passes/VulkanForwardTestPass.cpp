#include "ayapch.h"
#include "VulkanForwardTestPass.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Frustum.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Core/Application.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"

#include <algorithm>
#include <glm/glm.hpp>

namespace Ayaya {

    VulkanForwardTestPass::VulkanForwardTestPass() {
        m_PassName = "Vulkan Forward Test Pass";
    }

    void VulkanForwardTestPass::OnAttach() {
        m_ForwardShader = Shader::Create("Debug/debug.vert", "Debug/debug.frag");
        
        m_DefaultMaterial = std::make_shared<Material>();
        m_DefaultMaterial->Name = "Forward Default Material";

        // ==========================================
        // 1. 创建 Framebuffer (1 颜色 + 1 深度)
        // ==========================================
        FramebufferSpecification fboSpec;
        fboSpec.Samples = 1;
        fboSpec.Width = 1280; 
        fboSpec.Height = 720;
        fboSpec.Attachments = { 
            FramebufferTextureFormat::RGBA8, 
            FramebufferTextureFormat::Depth  
        };


        m_ForwardFBOs.resize(3);
        for (int i = 0; i < 3; i++) {
            m_ForwardFBOs[i] = Framebuffer::Create(fboSpec);
        }

        // ==========================================
        // 2. 打包 PSO (Pipeline State Object)
        // ==========================================
        PipelineSpecification pipeSpec;
        pipeSpec.Shader = m_ForwardShader;
        pipeSpec.TargetFramebuffer = m_ForwardFBOs[0];
        
        // ==========================================
        // 【核心修复 1】：必须声明顶点布局！
        // 如果这里不声明，VulkanPipeline 会认为这是个无顶点输入的管线
        // ==========================================
        pipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        
        pipeSpec.DepthTest = true;
        pipeSpec.DepthWrite = true;
        pipeSpec.Blend = false;
        pipeSpec.BackfaceCulling = CullMode::None;

        m_ForwardPipeline = Pipeline::Create(pipeSpec);
    }

    void VulkanForwardTestPass::OnResize(uint32_t width, uint32_t height) {
        for (auto& fbo : m_ForwardFBOs) {
            fbo->Resize(width, height);
        }
    }

    void VulkanForwardTestPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");
        if (width == 0 || height == 0) return;

        auto vulkanContext = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        // 确保这里的 3 与 m_ForwardFBOs 的大小一致
        uint32_t frameIndex = vulkanContext->GetCurrentFrameIndex() % m_ForwardFBOs.size();
        auto currentFBO = m_ForwardFBOs[frameIndex];
        if (currentFBO->GetSpecification().Width != width || currentFBO->GetSpecification().Height != height) {
            OnResize(width, height);
        }

        m_OpaqueDrawList.clear();

        glm::mat4 viewProj = context.ProjectionMatrix * context.ViewMatrix;
        Frustum cameraFrustum(viewProj);

        // ==========================================
        // 1. 场景图元收集与剔除
        // ==========================================
        auto meshGroup = context.ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
        for (auto entityID : meshGroup) {
            Entity entity{ entityID, context.ActiveScene.get() };
            if (!entity.IsActiveInHierarchy()) continue;

            auto& meshComp = entity.GetComponent<MeshRendererComponent>();
            if (!meshComp.ModelAsset) continue;

            glm::mat4 transform = entity.GetWorldTransform();
            
            // 简单的 Frustum Culling
            bool isVisible = false;
            for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                if (cameraFrustum.IsBoxVisible(mesh->GetAABB(), transform)) { isVisible = true; break; }
            }
            if (!isVisible) continue;

            auto targetMaterial = meshComp.MaterialAsset ? meshComp.MaterialAsset : m_DefaultMaterial;

            for (auto& mesh : meshComp.ModelAsset->GetMeshes()) {
                VulkanForwardCommandData drawCmd;
                drawCmd.Transform = transform;
                drawCmd.MeshAsset = mesh;
                drawCmd.MaterialAsset = targetMaterial;
                drawCmd.PipelineAsset = m_ForwardPipeline;
                drawCmd.TargetEntity = entity;
                m_OpaqueDrawList.push_back(drawCmd);
            }
        }

        // ==========================================
        // 2. 状态排序 (减少 Pipeline 切换)
        // ==========================================
        std::sort(m_OpaqueDrawList.begin(), m_OpaqueDrawList.end(), [](const auto& a, const auto& b) {
            if (a.PipelineAsset.get() != b.PipelineAsset.get()) return a.PipelineAsset.get() < b.PipelineAsset.get();
            if (a.MaterialAsset.get() != b.MaterialAsset.get()) return a.MaterialAsset.get() < b.MaterialAsset.get();
            return a.MeshAsset.get() < b.MeshAsset.get();
        });

        // ==========================================
        // 3. 执行绘制
        // ==========================================
        cmd.BeginRenderPass(currentFBO, true, glm::vec4(0.12f, 0.12f, 0.14f, 1.0f));

        std::shared_ptr<Pipeline> currentPipeline = nullptr;
        auto whiteTex = context.GetTexture("WhiteTexture");

        for (const auto& drawCmd : m_OpaqueDrawList) {
            
            if (currentPipeline != drawCmd.PipelineAsset) {
                currentPipeline = drawCmd.PipelineAsset;
                cmd.BindPipeline(currentPipeline);
                context.Stats.ShaderBinds++;
            }

            // ==========================================
            // 【核心修复 2】：抛弃 Material::Bind
            // 直接在这里解构材质属性，确保 Vulkan 描述符安全绑定！
            // ==========================================
            ForwardPushConstants constants{};
            constants.Transform = drawCmd.Transform;
            constants.Albedo = glm::vec3(1.0f);
            constants.UseAlbedoMap = 0;

            // 垫背操作：无论如何先塞一张白图，防止 Vulkan 抱怨贴图槽位未绑定！
            if (whiteTex) {
                cmd.BindTexture2D(currentPipeline, "u_AlbedoMap", 0, whiteTex);
            }

            if (drawCmd.MaterialAsset) {
                for (const auto& prop : drawCmd.MaterialAsset->Properties) {
                    if (prop.Type == MaterialPropertyType::Vec3 && prop.UniformName == "u_Albedo") {
                        constants.Albedo = prop.Vec3Value;
                    } 
                    else if (prop.Type == MaterialPropertyType::Texture2D && prop.UniformName == "u_AlbedoMap") {
                        bool hasValidTex = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) || (prop.RuntimeTexture != nullptr);
                        if (hasValidTex) {
                            auto tex = prop.RuntimeTexture ? prop.RuntimeTexture : AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                            // 假设 Forward Shader 里的贴图绑在 slot 0
                            cmd.BindTexture2D(currentPipeline, "u_AlbedoMap", 0, tex);
                            constants.UseAlbedoMap = 1;
                        }
                    }
                }
            }

            // 推送常量
            cmd.PushConstantData(currentPipeline, &constants, sizeof(ForwardPushConstants));

            // 绘制
            std::string tag = drawCmd.TargetEntity.GetComponent<TagComponent>().Tag;
            uint32_t tris = drawCmd.MeshAsset->GetIndexCount() / 3;

            if (context.RecordAndCheckDrawCall("Forward Test Pass", tag, "Forward Shader", tris)) {
                cmd.DrawIndexed(drawCmd.MeshAsset, drawCmd.MeshAsset->GetIndexCount());
            }
        }

        cmd.EndRenderPass();

        // ==========================================
        // 4. 将结果推到黑板，方便编辑器显示
        // ==========================================
        context.Set("Forward_Output", currentFBO); 
        // 将这个 Pass 的产物挂载为最终输出，这样 Editor 就会自动把这幅画贴到屏幕上
        context.Set("Final_Output", currentFBO);   
        context.Framebuffers["ForwardTest"] = currentFBO;
    }
}