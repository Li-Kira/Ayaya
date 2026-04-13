#pragma once
#include <memory>
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/Buffer.hpp"

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
    std::shared_ptr<Framebuffer> TargetFramebuffer;

    BufferLayout Layout;
    
    PrimitiveTopology Topology = PrimitiveTopology::Triangles;
    PolygonMode PolygonMode = PolygonMode::Fill;
    DepthCompareOperator DepthOperator = DepthCompareOperator::Less;
    
    bool DepthTest = true;
    bool DepthWrite = true;
    bool Blend = false;
    CullMode BackfaceCulling = CullMode::Back;
    BlendMode BlendMode = BlendMode::Alpha;

    // ==========================================
    // 【核心新增】：补充缺失的现代渲染状态
    // ==========================================
    bool DepthFuncLEqual = false;  // 用于天空盒等深度必须为 1.0 的渲染
    bool PolygonModeLine = false;  // 用于绘制线框（描边）
    float LineWidth = 1.0f;        // 线宽
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