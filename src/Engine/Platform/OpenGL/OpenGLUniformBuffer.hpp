#pragma once
#include "Renderer/UniformBuffer.hpp"

namespace Ayaya {

    class OpenGLUniformBuffer : public UniformBuffer {
    public:
        // 构造函数接收 size 和 binding
        OpenGLUniformBuffer(uint32_t size, uint32_t binding);
        virtual ~OpenGLUniformBuffer() override;

        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;

    private:
        uint32_t m_RendererID = 0;
        
        // ==========================================
        // 【核心修复】：新增成员变量，记住自己的槽位号！
        // ==========================================
        uint32_t m_Binding = 0; 
    };

}