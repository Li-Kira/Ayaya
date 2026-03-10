#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <glad/glad.h>
#include <glm/glm.hpp>

namespace Ayaya {

    class Shader {
    public:
        // 支持自动提取名称或手动指定名称
        Shader(const std::string& vertexPath, const std::string& fragmentPath);
        Shader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);
        ~Shader();

        void Bind() const;
        void Unbind() const;

        const std::string& GetName() const { return m_Name; }

        // --- Uniform 支持 ---
        void SetBool(const std::string& name, bool value);
        void SetInt(const std::string& name, int value);
        void SetIntArray(const std::string& name, int* values, uint32_t count);
        void SetFloat(const std::string& name, float value);
        void SetFloat2(const std::string& name, const glm::vec2& value);
        void SetFloat3(const std::string& name, const glm::vec3& value);
        void SetFloat4(const std::string& name, const glm::vec4& value);
        void SetMat2(const std::string& name, const glm::mat2& matrix);
        void SetMat3(const std::string& name, const glm::mat3& matrix);
        void SetMat4(const std::string& name, const glm::mat4& matrix);

        // 手动将 Shader 中的 Uniform Block 绑定到指定的槽位
        void BindUniformBlock(const std::string& name, uint32_t bindingPoint);

        // 静态工厂方法
        static std::shared_ptr<Shader> Create(const std::string& vertexPath, const std::string& fragmentPath);
        static std::shared_ptr<Shader> Create(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);

    private:
        uint32_t m_RendererID;
        std::string m_Name;
        mutable std::unordered_map<std::string, int> m_UniformLocationCache;

        // 核心初始化逻辑，解决赋值导致的资源销毁问题
        void Init(const std::string& vertexSource, const std::string& fragmentSource);
        
        int GetUniformLocation(const std::string& name) const;
        std::string ReadFile(const std::string& filepath);
        uint32_t CompileShader(uint32_t type, const std::string& source);
    };

    // --- ShaderLibrary ---
    class ShaderLibrary {
    public:
        void Add(const std::shared_ptr<Shader>& shader);
        void Add(const std::string& name, const std::shared_ptr<Shader>& shader);
        
        std::shared_ptr<Shader> Load(const std::string& vertexPath, const std::string& fragmentPath);
        std::shared_ptr<Shader> Load(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);

        std::shared_ptr<Shader> Get(const std::string& name);
        bool Exists(const std::string& name) const;
    private:
        std::unordered_map<std::string, std::shared_ptr<Shader>> m_Shaders;
    };
}