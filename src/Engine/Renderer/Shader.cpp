#include "ayapch.h"
#include "Shader.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLShader.hpp"
#include "Platform/Vulkan/VulkanShader.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    // 【新增】：内部辅助函数，负责将逻辑路径翻译为物理真实路径
    static std::string ResolveShaderPath(const std::string& logicalPath) {
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            return "assets/Editor/shaders/src/opengl/" + logicalPath;
        } 
        else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            return "assets/Editor/shaders/cache/vulkan/" + logicalPath + ".spv";
        }
        return logicalPath;
    }

    std::shared_ptr<Shader> Shader::Create(const std::string& vertexPath, const std::string& fragmentPath) {
        std::string realVPath = ResolveShaderPath(vertexPath);
        std::string realFPath = ResolveShaderPath(fragmentPath);

        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLShader>(realVPath, realFPath);
            case RendererAPI::API::Vulkan:  return std::make_shared<VulkanShader>(realVPath, realFPath); 
            case RendererAPI::API::Metal:   AYAYA_CORE_ERROR("Metal Shader is under construction!"); return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Shader> Shader::Create(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath) {
        std::string realVPath = ResolveShaderPath(vertexPath);
        std::string realFPath = ResolveShaderPath(fragmentPath);

        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLShader>(name, realVPath, realFPath);
            case RendererAPI::API::Vulkan:  return std::make_shared<VulkanShader>(name, realVPath, realFPath);
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