#include "ayapch.h"
#include "OpenGLPipeline.hpp"
#include <glad/glad.h>

namespace Ayaya {

    OpenGLPipeline::OpenGLPipeline(const PipelineSpecification& spec)
        : m_Specification(spec) {
        // 注：在 Vulkan 中，这里会调用 vkCreateGraphicsPipelines 产生真正的底层对象。
        // 但在 OpenGL 中，我们只需保存图纸，在 Bind 时应用即可。
    }

    void OpenGLPipeline::Bind() {
        // 1. 绑定 Shader
        if (m_Specification.Shader) {
            m_Specification.Shader->Bind();
        }

        // 2. 深度状态设置
        if (m_Specification.DepthTest) {
            glEnable(GL_DEPTH_TEST);
            glDepthMask(m_Specification.DepthWrite ? GL_TRUE : GL_FALSE);

            switch (m_Specification.DepthOperator) {
                case DepthCompareOperator::Less:    glDepthFunc(GL_LESS); break;
                case DepthCompareOperator::LEqual:  glDepthFunc(GL_LEQUAL); break;
                case DepthCompareOperator::Equal:   glDepthFunc(GL_EQUAL); break;
                case DepthCompareOperator::GEqual:  glDepthFunc(GL_GEQUAL); break;
                case DepthCompareOperator::Greater: glDepthFunc(GL_GREATER); break;
                case DepthCompareOperator::NotEqual:glDepthFunc(GL_NOTEQUAL); break;
                case DepthCompareOperator::Always:  glDepthFunc(GL_ALWAYS); break;
                default: glDepthFunc(GL_LESS); break;
            }
        } else {
            glDisable(GL_DEPTH_TEST);
        }

        // 3. 剔除状态
        if (m_Specification.BackfaceCulling != CullMode::None) {
            glEnable(GL_CULL_FACE);
            switch (m_Specification.BackfaceCulling) {
                case CullMode::Back:         glCullFace(GL_BACK); break;
                case CullMode::Front:        glCullFace(GL_FRONT); break;
                case CullMode::FrontAndBack: glCullFace(GL_FRONT_AND_BACK); break;
                default: glCullFace(GL_BACK); break;
            }
        } else {
            glDisable(GL_CULL_FACE);
        }

        // 4. 多边形模式与线宽
        if (m_Specification.PolygonMode == PolygonMode::Line) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(m_Specification.LineWidth);
        } else if (m_Specification.PolygonMode == PolygonMode::Point) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
        } else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // 5. 混合模式
        if (m_Specification.Blend) {
            glEnable(GL_BLEND);
            if (m_Specification.BlendMode == BlendModeType::Alpha) {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glBlendEquation(GL_FUNC_ADD);
            } else if (m_Specification.BlendMode == BlendModeType::Additive) {
                glBlendFunc(GL_ONE, GL_ONE); 
                glBlendEquation(GL_FUNC_ADD);
            }
        } else {
            glDisable(GL_BLEND);
        }
    }
}