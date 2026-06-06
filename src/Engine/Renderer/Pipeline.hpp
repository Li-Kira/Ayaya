#pragma once
#include <memory>
#include <vector>
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/Buffer.hpp"

namespace Ayaya {

    // ==========================================
    // 1. 现代 API 标准管线状态枚举 (必须放在结构体之前)
    // ==========================================
    enum class PrimitiveTopology { Triangles, Lines, TriangleStrip };
    enum class CullMode { None = 0, Front, Back, FrontAndBack };
    enum class PolygonMode { Fill, Line, Point };
    
    // 深度比较操作符：保留 LEqual, GEqual 等名称以兼容你现有的旧代码调用
    enum class DepthCompareOperator { 
        None = 0, 
        Less, 
        LEqual, 
        Equal, 
        GEqual, 
        Greater, 
        NotEqual, 
        Always 
    };
    
    // 混合模式：将类型重命名为 BlendModeType，解决成员变量同名导致的 C2653 错误
    enum class BlendModeType { None = 0, Alpha, Additive, PremultipliedAlpha,
        WBOITRevealage  // src=ZERO, dst=ONE_MINUS_SRC_COLOR (multiply-attenuation for WBOIT)
    };

    // ==========================================
    // 2. 管线图纸 (PipelineSpecification)
    // ==========================================
    struct PipelineSpecification {
        std::shared_ptr<Shader> Shader;
        std::shared_ptr<Framebuffer> TargetFramebuffer;

        BufferLayout Layout;

        PrimitiveTopology Topology = PrimitiveTopology::Triangles;
        PolygonMode PolygonMode = PolygonMode::Fill;

        // 【兼容性】：保留旧名称 DepthOperator，解决 C2039 报错
        DepthCompareOperator DepthOperator = DepthCompareOperator::Less;

        bool DepthTest = true;
        bool DepthWrite = true;
        bool Blend = false;

        CullMode BackfaceCulling = CullMode::Back;

        // 【兼容性】：变量名保持为 BlendMode，但类型使用 BlendModeType
        BlendModeType BlendMode = BlendModeType::Alpha;

        // Per-attachment blend overrides (WBOIT dual-attachment blend, etc.)
        // If non-empty, each attachment uses its own BlendMode; falls back to BlendMode otherwise.
        std::vector<BlendModeType> PerAttachmentBlend;

        // ==========================================
        // 【新增】：支持天空盒与高级渲染状态
        // ==========================================
        bool DepthFuncLEqual = false;  // 用于天空盒等深度必须为 1.0 的渲染
        bool PolygonModeLine = false;  // 快速线框模式开关
        float LineWidth = 1.0f;        // 线宽配置
        bool NoTextureDescriptors = false; // 跳过 Set 1 (纹理) 描述符集
        bool NoGlobalUBOs = false;          // 跳过 Set 0 (UBO) 描述符集 — UI 管线专用
        bool UseBindlessTextures = false;   // 使用全局 Bindless 纹理数组替代传统纹理绑定
    };

    // ==========================================
    // 3. 管线基类
    // ==========================================
    class Pipeline {
    public:
        virtual ~Pipeline() = default;

        virtual const PipelineSpecification& GetSpecification() const = 0;
        virtual PipelineSpecification& GetSpecification() = 0;

        virtual void Bind() = 0;

        static std::shared_ptr<Pipeline> Create(const PipelineSpecification& spec);
    };

}