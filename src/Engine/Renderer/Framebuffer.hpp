#pragma once

#include <memory>
#include <cstdint>

namespace Ayaya {

    struct FramebufferSpecification {
        uint32_t Width = 0;
        uint32_t Height = 0;
        // 以后可以加更多配置，比如 MSAA 采样数、贴图格式等
    };

    class Framebuffer {
    public:
        virtual ~Framebuffer() = default;

        virtual void Bind() = 0;
        virtual void Unbind() = 0;

        virtual void Resize(uint32_t width, uint32_t height) = 0;

        // 获取渲染好的那张“贴图”的 ID
        virtual uint32_t GetColorAttachmentRendererID() const = 0;

        virtual const FramebufferSpecification& GetSpecification() const = 0;

        static std::shared_ptr<Framebuffer> Create(const FramebufferSpecification& spec);
    };

}