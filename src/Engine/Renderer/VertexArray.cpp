#include "VertexArray.hpp"
#include <glad/glad.h>

namespace Ayaya {

    // 辅助函数：将 Ayaya 定义的 ShaderDataType 转换为 OpenGL 的基础类型
    static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type) {
        switch (type) {
            case ShaderDataType::Float:    return GL_FLOAT;
            case ShaderDataType::Float2:   return GL_FLOAT;
            case ShaderDataType::Float3:   return GL_FLOAT;
            case ShaderDataType::Float4:   return GL_FLOAT;
            case ShaderDataType::Mat3:     return GL_FLOAT;
            case ShaderDataType::Mat4:     return GL_FLOAT;
            case ShaderDataType::Int:      return GL_INT;
            case ShaderDataType::Int2:     return GL_INT;
            case ShaderDataType::Int3:     return GL_INT;
            case ShaderDataType::Int4:     return GL_INT;
            case ShaderDataType::Bool:     return GL_BOOL;
            default: break;
        }
        AYAYA_CORE_ERROR("Unknown ShaderDataType!");
        return 0;
    }

    class OpenGLVertexArray : public VertexArray {
    public:
        OpenGLVertexArray() {
            glGenVertexArrays(1, &m_RendererID);
        }

        virtual ~OpenGLVertexArray() {
            glDeleteVertexArrays(1, &m_RendererID);
        }

        virtual void Bind() const override {
            glBindVertexArray(m_RendererID);
        }

        virtual void Unbind() const override {
            glBindVertexArray(0);
        }

        virtual void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) override {
            if (vertexBuffer->GetLayout().GetElements().size() == 0) {
                AYAYA_CORE_ERROR("Vertex Buffer has no layout!");
                return;
            }

            glBindVertexArray(m_RendererID);
            vertexBuffer->Bind();

            uint32_t index = 0;
            const auto& layout = vertexBuffer->GetLayout();
            for (const auto& element : layout) {
                glEnableVertexAttribArray(index);
                glVertexAttribPointer(index,
                    element.GetComponentCount(),
                    ShaderDataTypeToOpenGLBaseType(element.Type),
                    element.Normalized ? GL_TRUE : GL_FALSE,
                    layout.GetStride(),
                    (const void*)element.Offset);
                index++;
            }

            m_VertexBuffers.push_back(vertexBuffer);
        }

        virtual void SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) override {
            glBindVertexArray(m_RendererID);
            indexBuffer->Bind();

            m_IndexBuffer = indexBuffer;
        }

        virtual const std::vector<std::shared_ptr<VertexBuffer>>& GetVertexBuffers() const override { return m_VertexBuffers; }
        virtual const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const override { return m_IndexBuffer; }

    private:
        uint32_t m_RendererID;
        std::vector<std::shared_ptr<VertexBuffer>> m_VertexBuffers;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };

    VertexArray* VertexArray::Create() {
        return new OpenGLVertexArray();
    }

}