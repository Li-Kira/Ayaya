#include "Buffer.hpp"
#include <glad/glad.h>

namespace Ayaya {

    // =========================================================================
    // VertexBuffer Implementation (OpenGL)
    // =========================================================================

    class OpenGLVertexBuffer : public VertexBuffer {
    public:
        OpenGLVertexBuffer(float* vertices, uint32_t size) {
            // 在 macOS (Apple Silicon) 上，建议先绑定 VAO 再操作 VBO
            glGenBuffers(1, &m_RendererID);
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
        }

        virtual ~OpenGLVertexBuffer() {
            glDeleteBuffers(1, &m_RendererID);
        }

        virtual void Bind() const override {
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        }

        virtual void Unbind() const override {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        virtual const BufferLayout& GetLayout() const override { return m_Layout; }
        virtual void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }

    private:
        uint32_t m_RendererID;
        BufferLayout m_Layout;
    };

    VertexBuffer* VertexBuffer::Create(float* vertices, uint32_t size) {
        return new OpenGLVertexBuffer(vertices, size);
    }

    // =========================================================================
    // IndexBuffer Implementation (OpenGL)
    // =========================================================================

    class OpenGLIndexBuffer : public IndexBuffer {
    public:
        OpenGLIndexBuffer(uint32_t* indices, uint32_t count)
            : m_Count(count) 
        {
            glGenBuffers(1, &m_RendererID);
            
            // 注意：GL_ELEMENT_ARRAY_BUFFER 必须在 VAO 绑定的情况下操作
            // 否则在某些驱动下可能会产生状态冲突
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
        }

        virtual ~OpenGLIndexBuffer() {
            glDeleteBuffers(1, &m_RendererID);
        }

        virtual void Bind() const override {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        }

        virtual void Unbind() const override {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }

        virtual uint32_t GetCount() const override { return m_Count; }

    private:
        uint32_t m_RendererID;
        uint32_t m_Count;
    };

    IndexBuffer* IndexBuffer::Create(uint32_t* indices, uint32_t count) {
        return new OpenGLIndexBuffer(indices, count);
    }

}