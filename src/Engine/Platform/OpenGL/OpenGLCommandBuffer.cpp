#include "ayapch.h"
#include "OpenGLCommandBuffer.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/TextureCube.hpp"
#include <glad/glad.h>

namespace Ayaya {
    // 辅助工具：用于动态解析 BufferLayout
    static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type) {
        switch (type) {
            case ShaderDataType::Float:    return GL_FLOAT;
            case ShaderDataType::Float2:   return GL_FLOAT;
            case ShaderDataType::Float3:   return GL_FLOAT;
            case ShaderDataType::Float4:   return GL_FLOAT;
            case ShaderDataType::Int:      return GL_INT;
            case ShaderDataType::Int2:     return GL_INT;
            case ShaderDataType::Int3:     return GL_INT;
            case ShaderDataType::Int4:     return GL_INT;
            case ShaderDataType::Bool:     return GL_BOOL;
            default: return 0;
        }
    }

    static uint32_t ShaderDataTypeComponentCount(ShaderDataType type) {
        switch (type) {
            case ShaderDataType::Float:    return 1;
            case ShaderDataType::Float2:   return 2;
            case ShaderDataType::Float3:   return 3;
            case ShaderDataType::Float4:   return 4;
            case ShaderDataType::Int:      return 1;
            case ShaderDataType::Int2:     return 2;
            case ShaderDataType::Int3:     return 3;
            case ShaderDataType::Int4:     return 4;
            case ShaderDataType::Bool:     return 1;
            default: return 0;
        }
    }

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

   // ==========================================
    // 现代 API 适配：从 Texture2D 对象提取 ID 绑定
    // ==========================================
    void OpenGLCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Texture2D>& texture) {
        if (!texture) return;
        
        uint32_t rendererID = texture->GetRendererID();
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, rendererID);

        // 【核心修复】：制导系统，确保贴图和 Shader 槽位精确对应！
        if (pipeline && pipeline->GetSpecification().Shader) {
            pipeline->GetSpecification().Shader->SetInt(name, slot);
        }
    }

    // ==========================================
    // 现代 API 适配：从 Framebuffer 的指定附件提取 ID 绑定
    // ==========================================
    void OpenGLCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Framebuffer>& framebuffer, uint32_t attachmentIndex, bool isDepth) {
        if (!framebuffer) return;
        
        uint32_t rendererID = 0;
        if (isDepth) {
            rendererID = (uint32_t)(uintptr_t)framebuffer->GetDepthAttachmentRendererID();
        } else {
            rendererID = (uint32_t)(uintptr_t)framebuffer->GetColorAttachmentRendererID(attachmentIndex);
        }
        
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, rendererID);

        // 【核心修复】：制导系统，确保贴图和 Shader 槽位精确对应！
        if (pipeline && pipeline->GetSpecification().Shader) {
            pipeline->GetSpecification().Shader->SetInt(name, slot);
        }
    }

    void OpenGLCommandBuffer::BindTextureCube(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<TextureCube>& textureCube) {
        if (!textureCube) return;

        glActiveTexture(GL_TEXTURE0 + slot);
        // 【核心提取】：从对象身上榨取数字 ID 喂给 OpenGL
        glBindTexture(GL_TEXTURE_CUBE_MAP, textureCube->GetRendererID()); 
        
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

    // 【现代 RHI 架构】：直接画 Mesh，动态组装 VAO
    void OpenGLCommandBuffer::DrawIndexed(const std::shared_ptr<Mesh>& mesh, uint32_t indexCount) {
        if (!mesh || !mesh->GetVertexBuffer() || !mesh->GetIndexBuffer()) return;

        // 1. OpenGL 核心模式要求必须有一个 VAO 绑定。我们用一个全局静态 VAO 作为载体。
        static GLuint s_VAO = 0;
        if (s_VAO == 0) glGenVertexArrays(1, &s_VAO);
        glBindVertexArray(s_VAO);

        // 2. 绑定底层数据
        mesh->GetVertexBuffer()->Bind();
        mesh->GetIndexBuffer()->Bind();

        // 3. 动态配置顶点属性布局 (模拟 VAO 的工作)
        const auto& layout = mesh->GetVertexBuffer()->GetLayout();
        uint32_t index = 0;
        for (const auto& element : layout) {
            glEnableVertexAttribArray(index);
            glVertexAttribPointer(
                index,
                ShaderDataTypeComponentCount(element.Type),
                ShaderDataTypeToOpenGLBaseType(element.Type),
                element.Normalized ? GL_TRUE : GL_FALSE,
                layout.GetStride(),
                (const void*)element.Offset
            );
            index++;
        }

        // 4. 发送绘制指令
        uint32_t count = indexCount ? indexCount : mesh->GetIndexCount();
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);

        // 5. 还原状态，防止污染
        glBindVertexArray(0);
    }

    void OpenGLCommandBuffer::DrawArrays(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount) {
        if (!mesh || !mesh->GetVertexBuffer()) return;

        static GLuint s_VAO = 0;
        if (s_VAO == 0) glGenVertexArrays(1, &s_VAO);
        glBindVertexArray(s_VAO);

        mesh->GetVertexBuffer()->Bind();
        const auto& layout = mesh->GetVertexBuffer()->GetLayout();
        uint32_t index = 0;
        for (const auto& element : layout) {
            glEnableVertexAttribArray(index);
            // 【核心修复 2】：规范排版，确保 6 个参数一个不少
            glVertexAttribPointer(
                index, 
                ShaderDataTypeComponentCount(element.Type), 
                ShaderDataTypeToOpenGLBaseType(element.Type), 
                element.Normalized ? GL_TRUE : GL_FALSE, 
                layout.GetStride(), 
                (const void*)element.Offset
            );
            index++;
        }

        uint32_t count = vertexCount ? vertexCount : mesh->GetVertexCount();
        glDrawArrays(GL_TRIANGLES, 0, count);
        glBindVertexArray(0);
    }

    void OpenGLCommandBuffer::DrawTriangleStrip(const std::shared_ptr<Mesh>& mesh, uint32_t vertexCount) {
        if (!mesh || !mesh->GetVertexBuffer()) return;

        static GLuint s_VAO = 0;
        if (s_VAO == 0) glGenVertexArrays(1, &s_VAO);
        glBindVertexArray(s_VAO);

        mesh->GetVertexBuffer()->Bind();
        const auto& layout = mesh->GetVertexBuffer()->GetLayout();
        uint32_t index = 0;
        for (const auto& element : layout) {
            glEnableVertexAttribArray(index);
            // 【核心修复 2】：规范排版，确保 6 个参数一个不少
            glVertexAttribPointer(
                index, 
                ShaderDataTypeComponentCount(element.Type), 
                ShaderDataTypeToOpenGLBaseType(element.Type), 
                element.Normalized ? GL_TRUE : GL_FALSE, 
                layout.GetStride(), 
                (const void*)element.Offset
            );
            index++;
        }

        uint32_t count = vertexCount ? vertexCount : mesh->GetVertexCount();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, count);
        glBindVertexArray(0);
    }

}