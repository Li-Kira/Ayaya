#pragma once

#include <string>
#include <unordered_map>
#include <glad/glad.h>
#include <glm/glm.hpp>

namespace Ayaya {

    class Shader {
    public:
        Shader(const std::string& vertexPath, const std::string& fragmentPath);
        ~Shader();

        void Bind() const;
        void Unbind() const;

        // --- Uniform 批量支持 ---
        
        // 标量
        void SetInt(const std::string& name, int value);
        void SetIntArray(const std::string& name, int* values, uint32_t count);
        void SetFloat(const std::string& name, float value);

        // 向量
        void SetFloat2(const std::string& name, const glm::vec2& value);
        void SetFloat3(const std::string& name, const glm::vec3& value);
        void SetFloat4(const std::string& name, const glm::vec4& value);

        // 矩阵
        void SetMat2(const std::string& name, const glm::mat2& matrix);
        void SetMat3(const std::string& name, const glm::mat3& matrix);
        void SetMat4(const std::string& name, const glm::mat4& matrix);

    private:
        uint32_t m_RendererID;
        // 缓存 Location 以避免性能抖动
        mutable std::unordered_map<std::string, int> m_UniformLocationCache;

        int GetUniformLocation(const std::string& name) const;
        std::string ReadFile(const std::string& filepath);
        uint32_t CompileShader(uint32_t type, const std::string& source);
    };

}