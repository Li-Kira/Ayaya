#include "ayapch.h"
#include "VulkanVertexArray.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    VulkanVertexArray::VulkanVertexArray() {
        // 作为纯数据容器，不需要做任何 Vulkan 相关的初始化
    }

    VulkanVertexArray::~VulkanVertexArray() {
    }

    void VulkanVertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) {
        AYAYA_CORE_ASSERT(vertexBuffer->GetLayout().GetElements().size(), "Vertex Buffer has no layout!");
        m_VertexBuffers.push_back(vertexBuffer);
    }

    void VulkanVertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) {
        m_IndexBuffer = indexBuffer;
    }

}