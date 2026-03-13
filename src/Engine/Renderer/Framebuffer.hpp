#pragma once

#include <memory>
#include <cstdint>

namespace Ayaya {

    // --- 新增：缓冲格式枚举 ---
    enum class FramebufferFormat {
        None = 0,
        RGBA8 = 1,
        RGBA16F = 2 // HDR 格式
    };

    struct FramebufferSpecification {
        uint32_t Width = 0, Height = 0;
        // 核心：抗锯齿采样率 (默认为 1，即不开启)
        uint32_t Samples = 1; 
        // --- 新增：默认使用普通的 RGBA8 ---
        FramebufferFormat Format = FramebufferFormat::RGBA8;
        bool SwapChainTarget = false;
    };

    class Framebuffer {
    public:
        virtual ~Framebuffer() = default;

        virtual void Bind() = 0;
        virtual void Unbind() = 0;

        virtual void Resize(uint32_t width, uint32_t height) = 0;

        // 纯虚函数，交由子类实现
        virtual uint32_t GetColorAttachmentRendererID() const = 0;
        // --- 新增：获取底层的 FBO ID ---
        virtual uint32_t GetRendererID() const = 0;
        virtual const FramebufferSpecification& GetSpecification() const = 0;

        static std::shared_ptr<Framebuffer> Create(const FramebufferSpecification& spec);
    };

}