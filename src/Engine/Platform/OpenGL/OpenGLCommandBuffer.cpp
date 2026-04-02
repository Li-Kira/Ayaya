#include "ayapch.h"
#include "OpenGLCommandBuffer.hpp"
#include <glad/glad.h>

namespace Ayaya {

    void OpenGLCommandBuffer::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) { glViewport(x, y, width, height); }
    void OpenGLCommandBuffer::SetDepthTest(bool enable) { if (enable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
    void OpenGLCommandBuffer::SetBlend(bool enable) { if (enable) glEnable(GL_BLEND); else glDisable(GL_BLEND); }
    void OpenGLCommandBuffer::SetDepthWrite(bool enable) { glDepthMask(enable ? GL_TRUE : GL_FALSE); }
    void OpenGLCommandBuffer::SetDepthFuncLEqual()       { glDepthFunc(GL_LEQUAL); }
    void OpenGLCommandBuffer::SetDepthFuncLess()         { glDepthFunc(GL_LESS); }
    void OpenGLCommandBuffer::SetBlendFuncAlpha()        { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    void OpenGLCommandBuffer::SetCullFaceFront()         { glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); }
    void OpenGLCommandBuffer::SetCullFaceBack()          { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
    void OpenGLCommandBuffer::SetCullFace(bool enable)   { if (enable) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
    void OpenGLCommandBuffer::SetPolygonModeLine()       { glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); }
    void OpenGLCommandBuffer::SetPolygonModeFill()       { glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); }
    void OpenGLCommandBuffer::SetLineWidth(float width)  { glLineWidth(width); }
    void OpenGLCommandBuffer::SetClearColor(const glm::vec4& color) { glClearColor(color.r, color.g, color.b, color.a); }
    void OpenGLCommandBuffer::Clear() { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT); }

    void OpenGLCommandBuffer::BlitDepth(uint32_t readFBO, uint32_t drawFBO, uint32_t width, uint32_t height) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFBO);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLCommandBuffer::BeginRenderPass(const std::shared_ptr<Framebuffer>& targetFBO, bool clear, const glm::vec4& clearColor) {
        if (targetFBO) {
            targetFBO->Bind();
            glViewport(0, 0, targetFBO->GetSpecification().Width, targetFBO->GetSpecification().Height);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        if (clear) {
            glDepthMask(GL_TRUE); 
            SetClearColor(clearColor);
            Clear();
        }
    }

    void OpenGLCommandBuffer::EndRenderPass() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

    void OpenGLCommandBuffer::BindPipeline(const std::shared_ptr<Pipeline>& pipeline) { if (pipeline) pipeline->Bind(); }

    void OpenGLCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, uint32_t rendererID) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, rendererID);
        if (pipeline) pipeline->GetSpecification().Shader->SetInt(name, slot);
    }

    void OpenGLCommandBuffer::BindTextureCube(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, uint32_t rendererID) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, rendererID);
        if (pipeline) pipeline->GetSpecification().Shader->SetInt(name, slot);
    }

    void OpenGLCommandBuffer::PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, float data) { if (pipeline) pipeline->GetSpecification().Shader->SetFloat(name, data); }
    void OpenGLCommandBuffer::PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, int data) { if (pipeline) pipeline->GetSpecification().Shader->SetInt(name, data); }
    void OpenGLCommandBuffer::PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec2& data) { if (pipeline) pipeline->GetSpecification().Shader->SetFloat2(name, data); }
    void OpenGLCommandBuffer::PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec3& data) { if (pipeline) pipeline->GetSpecification().Shader->SetFloat3(name, data); }
    void OpenGLCommandBuffer::PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::vec4& data) { if (pipeline) pipeline->GetSpecification().Shader->SetFloat4(name, data); }
    void OpenGLCommandBuffer::PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::mat3& data) { if (pipeline) pipeline->GetSpecification().Shader->SetMat3(name, data); }
    void OpenGLCommandBuffer::PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, const glm::mat4& data) { if (pipeline) pipeline->GetSpecification().Shader->SetMat4(name, data); }

    void OpenGLCommandBuffer::DrawArrays(uint32_t vertexCount) { glDrawArrays(GL_TRIANGLES, 0, vertexCount); }
    void OpenGLCommandBuffer::DrawIndexed(uint32_t indexCount) { glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr); }
    
    void OpenGLCommandBuffer::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount) {
        if (vertexArray) vertexArray->Bind();
        uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    }
    void OpenGLCommandBuffer::DrawArrays(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) {
        if (vertexArray) vertexArray->Bind();
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    }
    void OpenGLCommandBuffer::DrawTriangleStrip(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) {
        if (vertexArray) vertexArray->Bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, vertexCount);
    }
}