#include "ayapch.h"
#include "BloomPass.hpp"
#include "Renderer/RenderCommand.hpp"
#include <glad/glad.h>

namespace Ayaya {

    BloomPass::BloomPass() { m_PassName = "Bloom Pass"; }
    BloomPass::~BloomPass() { if (m_EmptyVAO != 0) glDeleteVertexArrays(1, &m_EmptyVAO); }

    void BloomPass::OnAttach() {
        glGenVertexArrays(1, &m_EmptyVAO);
        m_DownsampleShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/bloom_downsample.frag");
        m_UpsampleShader = Shader::Create("assets/Editor/shaders/PostProcess/postprocess.vert", "assets/Editor/shaders/PostProcess/bloom_upsample.frag");
    }

    void BloomPass::OnResize(uint32_t width, uint32_t height) {
        m_MipChain.clear();
        glm::vec2 mipSize((float)width / 2.0f, (float)height / 2.0f);
        glm::vec2 mipIntSize((uint32_t)mipSize.x, (uint32_t)mipSize.y);
        
        for (int i = 0; i < 6; i++) {
            BloomMip mip;
            mip.Size = mipSize; mip.IntSize = mipIntSize;
            
            FramebufferSpecification spec;
            spec.Samples = 1; 
            spec.Width = (uint32_t)mipIntSize.x; spec.Height = (uint32_t)mipIntSize.y;
            spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth }; 
            mip.FBO = Framebuffer::Create(spec);
            m_MipChain.push_back(mip);
            
            mipSize *= 0.5f; mipIntSize /= 2.0f;
        }
    }

    void BloomPass::Execute(RenderContext& context) {
        bool enableBloom = context.Get<bool>("EnableBloom", true);
        if (!enableBloom || m_MipChain.empty()) {
            context.Set("Bloom_Output", (uint32_t)0); 
            return;
        }

        uint32_t inputTextureID = context.Get<uint32_t>("Lighting_Output", 0);
        if (inputTextureID == 0) return;

        float threshold = context.Get<float>("BloomThreshold", 1.0f);
        float knee = context.Get<float>("BloomKnee", 0.1f);
        if (knee < 0.0001f) knee = 0.0001f;
        
        glm::vec3 curve;
        curve.x = threshold - knee;
        curve.y = knee * 2.0f;
        curve.z = 0.25f / knee;
        
        float bloomRadius = context.Get<float>("BloomRadius", 0.005f);

        glBindVertexArray(m_EmptyVAO);
        glDisable(GL_DEPTH_TEST);
        
        // 1. 降采样
        m_DownsampleShader->Bind();
        context.Stats.ShaderBinds++;
        m_DownsampleShader->SetInt("u_Image", 0);

        for (size_t i = 0; i < m_MipChain.size(); i++) {
            auto& mip = m_MipChain[i];
            mip.FBO->Bind();
            glViewport(0, 0, mip.IntSize.x, mip.IntSize.y);
            RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
            RenderCommand::Clear();

            // 精准传入层级，让 Shader 决定是否执行提取
            m_DownsampleShader->SetInt("u_MipLevel", (int)i);

            glActiveTexture(GL_TEXTURE0);
            if (i == 0) {
                m_DownsampleShader->SetFloat("u_Threshold", threshold);
                m_DownsampleShader->SetFloat3("u_Curve", curve);
                glBindTexture(GL_TEXTURE_2D, inputTextureID);
            } else {
                glBindTexture(GL_TEXTURE_2D, m_MipChain[i - 1].FBO->GetColorAttachmentRendererID(0));
            }

            glm::vec2 srcTexelSize = (i == 0) ? 
                glm::vec2(1.0f / context.Get<uint32_t>("ViewportWidth", 1280), 1.0f / context.Get<uint32_t>("ViewportHeight", 720)) : 
                glm::vec2(1.0f / m_MipChain[i - 1].Size.x, 1.0f / m_MipChain[i - 1].Size.y);
            m_DownsampleShader->SetFloat2("u_TexelSize", srcTexelSize);

            std::string stepName = "Downsample Mip " + std::to_string(i);
            if (context.RecordAndCheckDrawCall("Bloom Pass", stepName, "Downsample Shader", 1)) {
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
        }

        // 2. 升采样与混合
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glBlendEquation(GL_FUNC_ADD);

        m_UpsampleShader->Bind();
        context.Stats.ShaderBinds++;
        m_UpsampleShader->SetInt("u_Image", 0);
        m_UpsampleShader->SetFloat("u_FilterRadius", bloomRadius);
        
        for (int i = (int)m_MipChain.size() - 2; i >= 0; i--) {
            auto& currentMip = m_MipChain[i];
            auto& prevMip = m_MipChain[i + 1];

            currentMip.FBO->Bind(); 
            glViewport(0, 0, currentMip.IntSize.x, currentMip.IntSize.y);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, prevMip.FBO->GetColorAttachmentRendererID(0));

            std::string stepName = "Upsample Mip " + std::to_string(i);
            if (context.RecordAndCheckDrawCall("Bloom Pass", stepName, "Upsample Shader", 1)) {
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
        }

        glDisable(GL_BLEND);
        m_MipChain[0].FBO->Unbind();

        glViewport(0, 0, context.Get<uint32_t>("ViewportWidth", 1280), context.Get<uint32_t>("ViewportHeight", 720));
        context.Set("Bloom_Output", m_MipChain[0].FBO->GetColorAttachmentRendererID(0));
    }
}