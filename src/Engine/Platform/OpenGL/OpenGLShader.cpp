#include "ayapch.h"
#include "OpenGLShader.hpp"
#include "Core/Log.hpp"
#include <fstream>
#include <glm/gtc/type_ptr.hpp>

namespace Ayaya {

    static std::string ExtractName(const std::string& path) {
        auto lastSlash = path.find_last_of("/\\");
        lastSlash = (lastSlash == std::string::npos) ? 0 : lastSlash + 1;
        auto lastDot = path.rfind('.');
        auto count = (lastDot == std::string::npos) ? path.size() - lastSlash : lastDot - lastSlash;
        return path.substr(lastSlash, count);
    }

    OpenGLShader::OpenGLShader(const std::string& vertexPath, const std::string& fragmentPath)
        : m_Name(ExtractName(fragmentPath)) {
        Init(ReadFile(vertexPath), ReadFile(fragmentPath));
    }

    OpenGLShader::OpenGLShader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath)
        : m_Name(name) {
        Init(ReadFile(vertexPath), ReadFile(fragmentPath));
    }

    OpenGLShader::~OpenGLShader() {
        glDeleteProgram(m_RendererID);
    }

    std::string OpenGLShader::ReadFile(const std::string& filepath) {
        std::string result;
        std::ifstream in(filepath, std::ios::in | std::ios::binary);
        if (in) {
            in.seekg(0, std::ios::end);
            result.resize(in.tellg());
            in.seekg(0, std::ios::beg);
            in.read(&result[0], result.size());
            in.close();
        } else {
            AYAYA_CORE_ERROR("Could not open file '{0}'", filepath);
        }
        return result;
    }

    uint32_t OpenGLShader::CompileShader(uint32_t type, const std::string& source) {
        uint32_t id = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);
        int result;
        glGetShaderiv(id, GL_COMPILE_STATUS, &result);
        if (result == GL_FALSE) {
            int length;
            glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
            std::vector<char> error(length);
            glGetShaderInfoLog(id, length, &length, &error[0]);
            glDeleteShader(id);
            AYAYA_CORE_ERROR("Shader Compilation Failed: {0}", error.data());
            return 0;
        }
        return id;
    }

    void OpenGLShader::Init(const std::string& vertexSource, const std::string& fragmentSource) {
        uint32_t vs = CompileShader(GL_VERTEX_SHADER, vertexSource);
        uint32_t fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

        m_RendererID = glCreateProgram();
        glAttachShader(m_RendererID, vs);
        glAttachShader(m_RendererID, fs);
        glLinkProgram(m_RendererID);

        int isLinked = 0;
        glGetProgramiv(m_RendererID, GL_LINK_STATUS, &isLinked);
        if (isLinked == GL_FALSE) {
            int length = 0;
            glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &length);
            std::vector<char> error(length);
            glGetProgramInfoLog(m_RendererID, length, &length, &error[0]);
            glDeleteProgram(m_RendererID);
            glDeleteShader(vs);
            glDeleteShader(fs);
            AYAYA_CORE_ERROR("Shader Linking Failed: {0}", error.data());
            return;
        }

        glDetachShader(m_RendererID, vs);
        glDetachShader(m_RendererID, fs);
    }

    void OpenGLShader::Bind() const { glUseProgram(m_RendererID); }
    void OpenGLShader::Unbind() const { glUseProgram(0); }

    void OpenGLShader::SetBool(const std::string& name, bool value) { glUniform1i(GetUniformLocation(name), (int)value); }
    void OpenGLShader::SetInt(const std::string& name, int value) { glUniform1i(GetUniformLocation(name), value); }
    void OpenGLShader::SetIntArray(const std::string& name, int* values, uint32_t count) { glUniform1iv(GetUniformLocation(name), count, values); }
    void OpenGLShader::SetFloat(const std::string& name, float value) { glUniform1f(GetUniformLocation(name), value); }
    void OpenGLShader::SetFloat2(const std::string& name, const glm::vec2& value) { glUniform2f(GetUniformLocation(name), value.x, value.y); }
    void OpenGLShader::SetFloat3(const std::string& name, const glm::vec3& value) { glUniform3f(GetUniformLocation(name), value.x, value.y, value.z); }
    void OpenGLShader::SetFloat4(const std::string& name, const glm::vec4& value) { glUniform4f(GetUniformLocation(name), value.x, value.y, value.z, value.w); }
    void OpenGLShader::SetMat2(const std::string& name, const glm::mat2& matrix) { glUniformMatrix2fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix)); }
    void OpenGLShader::SetMat3(const std::string& name, const glm::mat3& matrix) { glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix)); }
    void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& matrix) { glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix)); }

    void OpenGLShader::BindUniformBlock(const std::string& name, uint32_t bindingPoint) {
        uint32_t index = glGetUniformBlockIndex(m_RendererID, name.c_str());
        if (index != GL_INVALID_INDEX) {
            glUniformBlockBinding(m_RendererID, index, bindingPoint);
        }
    }

    int OpenGLShader::GetUniformLocation(const std::string& name) const {
        if (m_UniformLocationCache.find(name) != m_UniformLocationCache.end())
            return m_UniformLocationCache[name];

        int location = glGetUniformLocation(m_RendererID, name.c_str());
        m_UniformLocationCache[name] = location;
        return location;
    }
}