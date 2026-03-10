#include "ayapch.h" // 确保使用了预编译头
#include "Shader.hpp"
#include "Core/Log.hpp"
#include <fstream>
#include <glm/gtc/type_ptr.hpp>

namespace Ayaya {

    // 从文件路径提取名称
    static std::string ExtractName(const std::string& path) {
        auto lastSlash = path.find_last_of("/\\");
        lastSlash = (lastSlash == std::string::npos) ? 0 : lastSlash + 1;
        auto lastDot = path.rfind('.');
        auto count = (lastDot == std::string::npos) ? path.size() - lastSlash : lastDot - lastSlash;
        return path.substr(lastSlash, count);
    }

    Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath)
        : m_Name(ExtractName(fragmentPath)) {
        Init(ReadFile(vertexPath), ReadFile(fragmentPath));
    }

    Shader::Shader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath)
        : m_Name(name) {
        Init(ReadFile(vertexPath), ReadFile(fragmentPath));
    }

    void Shader::Init(const std::string& vertexSource, const std::string& fragmentSource) {
        uint32_t vs = CompileShader(GL_VERTEX_SHADER, vertexSource);
        uint32_t fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

        m_RendererID = glCreateProgram();
        glAttachShader(m_RendererID, vs);
        glAttachShader(m_RendererID, fs);
        glLinkProgram(m_RendererID);

        int isLinked = 0;
        glGetProgramiv(m_RendererID, GL_LINK_STATUS, &isLinked);
        if (isLinked == GL_FALSE) {
            int maxLength = 0;
            glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &maxLength);
            std::vector<char> infoLog(maxLength);
            glGetProgramInfoLog(m_RendererID, maxLength, &maxLength, &infoLog[0]);
            AYAYA_CORE_ERROR("Shader link failure ({0}): {1}", m_Name, infoLog.data());
            
            glDeleteProgram(m_RendererID);
            glDeleteShader(vs);
            glDeleteShader(fs);
            return;
        }

        glDetachShader(m_RendererID, vs);
        glDetachShader(m_RendererID, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    Shader::~Shader() { glDeleteProgram(m_RendererID); }

    void Shader::Bind() const { glUseProgram(m_RendererID); }
    void Shader::Unbind() const { glUseProgram(0); }

    // --- Uniforms ---
    void Shader::SetBool(const std::string& name, bool value) { glUniform1i(GetUniformLocation(name), (int)value); }
    void Shader::SetInt(const std::string& name, int value) { glUniform1i(GetUniformLocation(name), value); }
    void Shader::SetIntArray(const std::string& name, int* values, uint32_t count) { glUniform1iv(GetUniformLocation(name), count, values); }
    void Shader::SetFloat(const std::string& name, float value) { glUniform1f(GetUniformLocation(name), value); }
    void Shader::SetFloat2(const std::string& name, const glm::vec2& value) { glUniform2f(GetUniformLocation(name), value.x, value.y); }
    void Shader::SetFloat3(const std::string& name, const glm::vec3& value) { glUniform3f(GetUniformLocation(name), value.x, value.y, value.z); }
    void Shader::SetFloat4(const std::string& name, const glm::vec4& value) { glUniform4f(GetUniformLocation(name), value.x, value.y, value.z, value.w); }
    void Shader::SetMat4(const std::string& name, const glm::mat4& matrix) { glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix)); }
    // (其他 Mat2, Mat3 类似实现...)

    void Shader::BindUniformBlock(const std::string& name, uint32_t bindingPoint) {
        // 1. 查询这个 Uniform Block 在当前 Shader 中的索引 ID
        uint32_t blockIndex = glGetUniformBlockIndex(m_RendererID, name.c_str());
        
        // 2. 如果找到了，就把它挂载到我们指定的槽位上（比如 0）
        if (blockIndex != GL_INVALID_INDEX) {
            glUniformBlockBinding(m_RendererID, blockIndex, bindingPoint);
        } else {
            // 可以选择打个警告，说明这个 Shader 没用到这个 Block
            // AYAYA_CORE_WARN("Uniform block '{0}' not found in shader!", name);
        }
    }

    int Shader::GetUniformLocation(const std::string& name) const {
        if (m_UniformLocationCache.count(name)) return m_UniformLocationCache[name];
        int location = glGetUniformLocation(m_RendererID, name.c_str());
        if (location == -1) AYAYA_CORE_WARN("Uniform '{0}' not found!", name);
        m_UniformLocationCache[name] = location;
        return location;
    }

    std::shared_ptr<Shader> Shader::Create(const std::string& vertexPath, const std::string& fragmentPath) {
        return std::make_shared<Shader>(vertexPath, fragmentPath);
    }

    std::shared_ptr<Shader> Shader::Create(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath) {
        return std::make_shared<Shader>(name, vertexPath, fragmentPath);
    }

    // --- ShaderLibrary 实现 ---
    void ShaderLibrary::Add(const std::string& name, const std::shared_ptr<Shader>& shader) {
        if (Exists(name)) { AYAYA_CORE_WARN("Shader '{0}' already exists!", name); return; }
        m_Shaders[name] = shader;
    }

    void ShaderLibrary::Add(const std::shared_ptr<Shader>& shader) { Add(shader->GetName(), shader); }

    std::shared_ptr<Shader> ShaderLibrary::Load(const std::string& vertexPath, const std::string& fragmentPath) {
        auto shader = Shader::Create(vertexPath, fragmentPath);
        Add(shader);
        return shader;
    }

    std::shared_ptr<Shader> ShaderLibrary::Load(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath) {
        auto shader = Shader::Create(name, vertexPath, fragmentPath);
        Add(name, shader);
        return shader;
    }

    std::shared_ptr<Shader> ShaderLibrary::Get(const std::string& name) {
        if (!Exists(name)) { AYAYA_CORE_ERROR("Shader '{0}' not found!", name); return nullptr; }
        return m_Shaders[name];
    }

    bool ShaderLibrary::Exists(const std::string& name) const { return m_Shaders.count(name) > 0; }

    // 原有的 ReadFile 和 CompileShader 逻辑保持不变...
    std::string Shader::ReadFile(const std::string& filepath) {
        std::string result;
        std::ifstream in(filepath, std::ios::in | std::ios::binary);
        if (in) {
            in.seekg(0, std::ios::end);
            result.resize(in.tellg());
            in.seekg(0, std::ios::beg);
            in.read(&result[0], result.size());
            in.close();
        } else { AYAYA_CORE_ERROR("Could not open file '{0}'", filepath); }
        return result;
    }

    uint32_t Shader::CompileShader(uint32_t type, const std::string& source) {
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
            AYAYA_CORE_ERROR("Compile failure: {0}", message.data());
            glDeleteShader(id);
            return 0;
        }
        return id;
    }
}