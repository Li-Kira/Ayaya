#pragma once
#include "Renderer/RenderPipeline.hpp"

namespace Ayaya {
    class VulkanClearPass : public RenderPass {
    public:
        VulkanClearPass() {
            m_PassName = "Vulkan Clear Pass";
        }
        virtual ~VulkanClearPass() override = default;

        // 【核心修复】：必须实现基类的纯虚函数，否则会报抽象类错误
        virtual void OnAttach() override {}
        virtual void OnResize(uint32_t width, uint32_t height) override {}
        
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
    };
}