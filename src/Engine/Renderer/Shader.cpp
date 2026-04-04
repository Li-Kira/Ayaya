#include "ayapch.h"
#include "Shader.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Platform/OpenGL/OpenGLShader.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    std::shared_ptr<Shader> Shader::Create(const std::string& vertexPath, const std::string& fragmentPath) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLShader>(vertexPath, fragmentPath);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan Shader is under construction!"); return nullptr;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal Shader is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Shader> Shader::Create(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLShader>(name, vertexPath, fragmentPath);
            case RendererAPI::API::Vulkan:  AYAYA_CORE_ERROR("Vulkan Shader is under construction!"); return nullptr;
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal Shader is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    // --- ShaderLibrary 实现 ---
    void ShaderLibrary::Add(const std::string& name, const std::shared_ptr<Shader>& shader) {
        if (Exists(name)) { AYAYA_CORE_ERROR("Shader '{0}' already exists!", name); return; }
        m_Shaders[name] = shader;
    }

    void ShaderLibrary::Add(const std::shared_ptr<Shader>& shader) {
        Add(shader->GetName(), shader);
    }

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

    bool ShaderLibrary::Exists(const std::string& name) const { 
        return m_Shaders.count(name) > 0; 
    }
}