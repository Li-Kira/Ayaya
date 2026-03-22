#pragma once

#include <memory>
#include <cstdint>
#include <vector>
#include <initializer_list>

namespace Ayaya {

    // ==========================================
    // 1. 扩充：支持 G-Buffer 所需的各种贴图精度
    // ==========================================
    enum class FramebufferTextureFormat {
        None = 0,
        // 颜色通道
        RGBA8,
        RGBA16F, // HDR 颜色 / 法线
        RGBA32F, // 世界坐标 (高精度)
        RED_INTEGER, // 实体 ID (用于鼠标拾取，后续可加)
        // 深度/模板
        DEPTH24STENCIL8,
        // 默认深度
        Depth = DEPTH24STENCIL8
    };

    struct FramebufferTextureSpecification {
        FramebufferTextureSpecification() = default;
        FramebufferTextureSpecification(FramebufferTextureFormat format) : TextureFormat(format) {}
        FramebufferTextureFormat TextureFormat = FramebufferTextureFormat::None;
        // 未来可以在这里扩展纹理包裹方式 (Wrap) 和过滤方式 (Filter)
    };

    // ==========================================
    // 2. 核心：附件集合 (代替原来的单一 Format)
    // ==========================================
    struct FramebufferAttachmentSpecification {
        FramebufferAttachmentSpecification() = default;
        FramebufferAttachmentSpecification(std::initializer_list<FramebufferTextureSpecification> attachments)
            : Attachments(attachments) {}

        std::vector<FramebufferTextureSpecification> Attachments;
    };

    struct FramebufferSpecification {
        uint32_t Width = 0, Height = 0;
        uint32_t Samples = 1; 
        
        // 替换了原来的 Format
        FramebufferAttachmentSpecification Attachments; 
        
        bool SwapChainTarget = false;
    };

    class Framebuffer {
    public:
        virtual ~Framebuffer() = default;

        virtual void Bind() = 0;
        virtual void Unbind() = 0;

        virtual void Resize(uint32_t width, uint32_t height) = 0;

        // ==========================================
        // 修改：现在可以根据索引获取对应的 G-Buffer 贴图了！
        // ==========================================
        virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const = 0;
        
        virtual uint32_t GetRendererID() const = 0;
        virtual const FramebufferSpecification& GetSpecification() const = 0;

        static std::shared_ptr<Framebuffer> Create(const FramebufferSpecification& spec);
    };

}