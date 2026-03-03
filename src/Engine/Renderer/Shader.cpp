#include "Shader.hpp"
#include "Core/Log.hpp"
#include <fstream>
#include <sstream>
#include <vector>

namespace Ayaya {

    Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
        std::string vertexSource = ReadFile(vertexPath);
        std::string fragmentSource = ReadFile(fragmentPath);

        uint32_t vs = CompileShader(GL_VERTEX_SHADER, vertexSource);
        uint32_t fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

        m_RendererID = glCreateProgram();
        glAttachShader(m_RendererID, vs);
        glAttachShader(m_RendererID, fs);
        glLinkProgram(m_RendererID);
        glValidateProgram(m_RendererID);

        glDeleteShader(vs);
        glDeleteShader(fs);
        
        AYAYA_CORE_INFO("Shader program created: ID {0}", m_RendererID);
    }

    Shader::~Shader() {
        glDeleteProgram(m_RendererID);
    }

    std::string Shader::ReadFile(const std::string& filepath) {
        std::string result;
        std::ifstream in(filepath, std::ios::in | std::ios::binary);
        if (in) {
            std::ostringstream contents;
            contents << in.rdbuf();
            result = contents.str();
        } else {
            AYAYA_CORE_ERROR("Could not open file: '{0}'", filepath);
        }
        return result;
    }

    uint32_t Shader::CompileShader(unsigned int type, const std::string& source) {
        uint32_t id = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);

        int result;
        glGetShaderiv(id, GL_COMPILE_STATUS, &result);
        if (result == GL_FALSE) {
            int length;
            glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
            std::vector<char> message(length);
            glGetShaderInfoLog(id, length, &length, message.data());
            AYAYA_CORE_ERROR("Failed to compile {0} shader!", (type == GL_VERTEX_SHADER ? "vertex" : "fragment"));
            AYAYA_CORE_ERROR(message.data());
            glDeleteShader(id);
            return 0;
        }
        return id;
    }

    void Shader::Bind() const { glUseProgram(m_RendererID); }
    void Shader::Unbind() const { glUseProgram(0); }
}