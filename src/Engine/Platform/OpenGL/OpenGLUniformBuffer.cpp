#include "ayapch.h"
#include "OpenGLUniformBuffer.hpp"
#include <glad/glad.h>

namespace Ayaya {

    // ==========================================
    // 【核心修复】：在构造时，将 binding 存入 m_Binding
    // ==========================================
    OpenGLUniformBuffer::OpenGLUniformBuffer(uint32_t size, uint32_t binding)
        : m_Binding(binding) 
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_UNIFORM_BUFFER, m_RendererID);
        
        // 分配显存空间
        glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
        
        // 顺手做一次初始绑定
        glBindBufferBase(GL_UNIFORM_BUFFER, m_Binding, m_RendererID);
    }

    OpenGLUniformBuffer::~OpenGLUniformBuffer() {
        glDeleteBuffers(1, &m_RendererID);
    }

    void OpenGLUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset) {
        // 1. 【兼容 4.1 写法】：先将这个 Buffer 绑定到通用目标上
        glBindBuffer(GL_UNIFORM_BUFFER, m_RendererID);
        
        // 2. 向当前绑定的 Buffer 写入数据
        glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);

        // 3. 【防劫持核心】：写完后，强行夺回自己的全局槽位
        // 此时 m_Binding 已经被构造函数正确赋值（如 0 或 1），不再报错！
        glBindBufferBase(GL_UNIFORM_BUFFER, m_Binding, m_RendererID);
    }

}