#pragma once
#include "Renderer/Shader.hpp"
#include <glad/glad.h>

namespace Ayaya {

    class OpenGLShader : public Shader {
    public:
        OpenGLShader(const std::string& vertexPath, const std::string& fragmentPath);
        OpenGLShader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);
        virtual ~OpenGLShader();

        virtual void Bind() const override;
        virtual void Unbind() const override;

        virtual const std::string& GetName() const override { return m_Name; }

        virtual void SetBool(const std::string& name, bool value) override;
        virtual void SetInt(const std::string& name, int value) override;
        virtual void SetIntArray(const std::string& name, int* values, uint32_t count) override;
        virtual void SetFloat(const std::string& name, float value) override;
        virtual void SetFloat2(const std::string& name, const glm::vec2& value) override;
        virtual void SetFloat3(const std::string& name, const glm::vec3& value) override;
        virtual void SetFloat4(const std::string& name, const glm::vec4& value) override;
        virtual void SetMat2(const std::string& name, const glm::mat2& matrix) override;
        virtual void SetMat3(const std::string& name, const glm::mat3& matrix) override;
        virtual void SetMat4(const std::string& name, const glm::mat4& matrix) override;

        virtual void BindUniformBlock(const std::string& name, uint32_t bindingPoint) override;

    private:
        uint32_t m_RendererID;
        std::string m_Name;
        mutable std::unordered_map<std::string, int> m_UniformLocationCache;

        void Init(const std::string& vertexSource, const std::string& fragmentSource);
        int GetUniformLocation(const std::string& name) const;
        std::string ReadFile(const std::string& filepath);
        uint32_t CompileShader(uint32_t type, const std::string& source);
    };

}