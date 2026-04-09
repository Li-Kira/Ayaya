#pragma once
#include "Renderer/VertexArray.hpp"

namespace Ayaya {

    class VulkanVertexArray : public VertexArray {
    public:
        VulkanVertexArray();
        virtual ~VulkanVertexArray() override;

        // Vulkan 不需要像 OpenGL 那样绑定 VAO
        virtual void Bind() const override {}
        virtual void Unbind() const override {}

        virtual void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) override;
        virtual void SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) override;

        virtual const std::vector<std::shared_ptr<VertexBuffer>>& GetVertexBuffers() const override { return m_VertexBuffers; }
        virtual const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const override { return m_IndexBuffer; }

    private:
        std::vector<std::shared_ptr<VertexBuffer>> m_VertexBuffers;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };

}