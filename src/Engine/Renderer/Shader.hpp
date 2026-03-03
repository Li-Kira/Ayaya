#pragma once

#include <string>
#include <glad/glad.h>

namespace Ayaya {

    class Shader {
    public:
        Shader(const std::string& vertexPath, const std::string& fragmentPath);
        ~Shader();

        void Bind() const;
        void Unbind() const;

        // 以后用于传递 Uniform 变量
        void SetInt(const std::string& name, int value);
        void SetFloat(const std::string& name, float value);

    private:
        uint32_t m_RendererID; // OpenGL 程序 ID
        std::string ReadFile(const std::string& filepath);
        uint32_t CompileShader(unsigned int type, const std::string& source);
    };

}