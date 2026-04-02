#pragma once

#include "Renderer/Buffer.hpp"
#include <memory>
#include <vector>

namespace Ayaya {

    class VertexArray {
    public:
        virtual ~VertexArray() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        // 向 VAO 添加顶点缓冲区
        virtual void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) = 0;
        
        // 设置索引缓冲区
        virtual void SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) = 0;

        virtual const std::vector<std::shared_ptr<VertexBuffer>>& GetVertexBuffers() const = 0;
        virtual const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const = 0;

        // 【核心修改】：返回安全的智能指针！
        static std::shared_ptr<VertexArray> Create();
    };

}