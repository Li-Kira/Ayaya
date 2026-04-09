#include "ayapch.h"
#include "VulkanRenderCommandBuffer.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    VulkanRenderCommandBuffer::VulkanRenderCommandBuffer() {
        AYAYA_CORE_WARN("VulkanRenderCommandBuffer created (Stub)");
    }

    VulkanRenderCommandBuffer::~VulkanRenderCommandBuffer() {
    }

    void VulkanRenderCommandBuffer::Begin() {
        // 占位逻辑：未来我们会在这里调用 vkBeginCommandBuffer (如果使用的是 Secondary Command Buffers)
    }

    void VulkanRenderCommandBuffer::End() {
        // 占位逻辑：未来我们会在这里调用 vkEndCommandBuffer
    }

}