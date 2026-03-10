#include "ayapch.h"
#include "OpenGLUniformBuffer.hpp"
#include <glad/glad.h>

namespace Ayaya {

    OpenGLUniformBuffer::OpenGLUniformBuffer(uint32_t size, uint32_t binding) {
        // [OpenGL 4.1 兼容] 使用 glGenBuffers 替代 glCreateBuffers
        glGenBuffers(1, &m_RendererID);
        
        // 必须先绑定才能操作
        glBindBuffer(GL_UNIFORM_BUFFER, m_RendererID);
        
        // [OpenGL 4.1 兼容] 使用 glBufferData 替代 glNamedBufferData
        glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW); 
        
        // glBindBufferBase 在 OpenGL 3.1 就有了，可以直接用
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, m_RendererID); 
    }

    OpenGLUniformBuffer::~OpenGLUniformBuffer() {
        glDeleteBuffers(1, &m_RendererID);
    }

    void OpenGLUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset) {
        // [OpenGL 4.1 兼容] 每次更新数据前必须重新绑定！
        // 使用 glBufferSubData 替代 glNamedBufferSubData
        glBindBuffer(GL_UNIFORM_BUFFER, m_RendererID);
        glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
    }

}