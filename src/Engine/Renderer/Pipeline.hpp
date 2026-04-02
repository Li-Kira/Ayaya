#pragma once
#include <memory>
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"

namespace Ayaya {

    // --- 现代 API 标准管线状态枚举 ---
    enum class PrimitiveTopology { Triangles, Lines, TriangleStrip };
    enum class CullMode { None = 0, Front, Back, FrontAndBack };
    enum class PolygonMode { Fill, Line, Point };
    enum class DepthCompareOperator { None = 0, Less, LEqual, Equal, GEqual, Greater, NotEqual, Always };
    enum class BlendMode { None = 0, Alpha, Additive };

    // --- 管线图纸 (Blueprint) ---
    struct PipelineSpecification {
        std::shared_ptr<Shader> Shader;
        std::shared_ptr<Framebuffer> TargetFramebuffer; // 决定了输出的格式和渲染通道兼容性

        PrimitiveTopology Topology = PrimitiveTopology::Triangles;
        PolygonMode PolygonMode = PolygonMode::Fill;
        CullMode BackfaceCulling = CullMode::Back;
        float LineWidth = 1.0f;

        bool DepthTest = true;
        bool DepthWrite = true;
        DepthCompareOperator DepthOperator = DepthCompareOperator::Less;

        bool Blend = false;
        BlendMode BlendMode = BlendMode::Alpha;
    };

    // --- 管线基类 ---
    class Pipeline {
    public:
        virtual ~Pipeline() = default;

        virtual const PipelineSpecification& GetSpecification() const = 0;
        
        // 核心接口：一键应用所有状态
        virtual void Bind() = 0; 

        static std::shared_ptr<Pipeline> Create(const PipelineSpecification& spec);
    };

}