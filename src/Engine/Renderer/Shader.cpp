#include "ayapch.h"
#include "Shader.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLShader.hpp"
#include "Platform/Vulkan/VulkanShader.hpp"
#include "Core/Log.hpp"
#include "Core/VFS.hpp"

namespace Ayaya {

    // 【智能解析器】：负责将逻辑路径翻译为 VFS 虚拟路径，并最终解析为真实物理路径
    static std::string ResolveShaderPath(const std::string& logicalPath) {
        std::string virtualPath = logicalPath;

        // 获取协议头
        std::string scheme = VFS::GetScheme(logicalPath);

        // 【极简逻辑】：如果没有任何协议头（比如传入 "Skybox/skybox.vert"）
        // 我们将其视为 C++ 底层无感调用的引擎内置 Shader，自动为其补全 API 路径和协议头
        if (scheme.empty()) {
            if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
                virtualPath = "engine://Editor/shaders/src/opengl/" + logicalPath;
            } else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
                // Check project-local shader cache first (TA-authored HLSL/GLSL)
                std::string projectLocal = "project://Shaders/Cache/" + logicalPath + ".spv";
                std::string resolved = VFS::ResolveString(projectLocal);
                if (std::filesystem::exists(resolved))
                    return resolved;
                // Fall back to engine global shader cache
                virtualPath = "engine://Editor/shaders/cache/vulkan/" + logicalPath + ".spv";
            }
        }
        // 如果 scheme 是 "project"，说明用户传入了 "project://shaders/my_shader.glsl"
        // 那么它会跳过补全，直接交给下面的 VFS 解析为项目物理路径。

        // 移交 VFS：将虚拟路径转换为当前电脑硬盘上的绝对路径
        return VFS::ResolveString(virtualPath);
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