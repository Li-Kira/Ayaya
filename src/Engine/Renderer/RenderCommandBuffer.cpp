#include "ayapch.h"
#include "RenderCommandBuffer.hpp"
#include <glad/glad.h>

namespace Ayaya {

    void RenderCommandBuffer::Begin() {}
    void RenderCommandBuffer::End() {}

    void RenderCommandBuffer::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) { glViewport(x, y, width, height); }
    void RenderCommandBuffer::SetDepthTest(bool enable) { if (enable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
    void RenderCommandBuffer::SetBlend(bool enable) { if (enable) glEnable(GL_BLEND); else glDisable(GL_BLEND); }
    
    void RenderCommandBuffer::SetDepthWrite(bool enable) { glDepthMask(enable ? GL_TRUE : GL_FALSE); }
    void RenderCommandBuffer::SetDepthFuncLEqual()       { glDepthFunc(GL_LEQUAL); }
    void RenderCommandBuffer::SetDepthFuncLess()         { glDepthFunc(GL_LESS); }
    void RenderCommandBuffer::SetBlendFuncAlpha()        { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    
    void RenderCommandBuffer::SetCullFaceFront()         { glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); }
    void RenderCommandBuffer::SetCullFaceBack()          { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
    void RenderCommandBuffer::SetCullFace(bool enable)   { if (enable) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
    
    void RenderCommandBuffer::SetPolygonModeLine()       { glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); }
    void RenderCommandBuffer::SetPolygonModeFill()       { glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); }
    void RenderCommandBuffer::SetLineWidth(float width)  { glLineWidth(width); }

    void RenderCommandBuffer::SetClearColor(const glm::vec4& color) { glClearColor(color.r, color.g, color.b, color.a); }
    void RenderCommandBuffer::Clear() { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }

    void RenderCommandBuffer::BlitDepth(uint32_t readFBO, uint32_t drawFBO, uint32_t width, uint32_t height) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFBO);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }

    void RenderCommandBuffer::BindTexture2D(uint32_t slot, uint32_t rendererID) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, rendererID);
    }

    void RenderCommandBuffer::DrawArrays(uint32_t vertexCount) { glDrawArrays(GL_TRIANGLES, 0, vertexCount); }
    void RenderCommandBuffer::DrawIndexed(uint32_t indexCount) { glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr); }

    void RenderCommandBuffer::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount) {
        if (vertexArray) vertexArray->Bind();
        uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    }

    void RenderCommandBuffer::DrawArrays(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) {
        if (vertexArray) vertexArray->Bind();
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    }

    void RenderCommandBuffer::DrawTriangleStrip(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) {
        if (vertexArray) vertexArray->Bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, vertexCount);
    }

}