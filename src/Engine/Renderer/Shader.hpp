#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>

namespace Ayaya {

    // ==========================================
    // 纯虚基类：抹除任何 glad/GL 痕迹
    // ==========================================
    class Shader {
    public:
        virtual ~Shader() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        virtual const std::string& GetName() const = 0;

        virtual void SetBool(const std::string& name, bool value) = 0;
        virtual void SetInt(const std::string& name, int value) = 0;
        virtual void SetIntArray(const std::string& name, int* values, uint32_t count) = 0;
        virtual void SetFloat(const std::string& name, float value) = 0;
        virtual void SetFloat2(const std::string& name, const glm::vec2& value) = 0;
        virtual void SetFloat3(const std::string& name, const glm::vec3& value) = 0;
        virtual void SetFloat4(const std::string& name, const glm::vec4& value) = 0;
        virtual void SetMat2(const std::string& name, const glm::mat2& matrix) = 0;
        virtual void SetMat3(const std::string& name, const glm::mat3& matrix) = 0;
        virtual void SetMat4(const std::string& name, const glm::mat4& matrix) = 0;

        virtual void BindUniformBlock(const std::string& name, uint32_t bindingPoint) = 0;

        // 静态工厂
        static std::shared_ptr<Shader> Create(const std::string& vertexPath, const std::string& fragmentPath);
        static std::shared_ptr<Shader> Create(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);
    };

    // --- ShaderLibrary 保持不变 (它只管理 shared_ptr) ---
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