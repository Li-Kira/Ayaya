#pragma once
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    class OpenGLPipeline : public Pipeline {
    public:
        OpenGLPipeline(const PipelineSpecification& spec);
        virtual ~OpenGLPipeline() override = default;

        virtual const PipelineSpecification& GetSpecification() const override { return m_Specification; }
        
        // 将抽象图纸翻译为底层的 OpenGL 状态机调用
        virtual void Bind() override;

    private:
        PipelineSpecification m_Specification;
    };

}