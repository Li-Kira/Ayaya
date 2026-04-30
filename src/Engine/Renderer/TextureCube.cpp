#include "ayapch.h"
#include "TextureCube.hpp"
#include "Renderer/Renderer.hpp"
#include "Platform/OpenGL/OpenGLTextureCube.hpp"
#include "Platform/Vulkan/VulkanTextureCube.hpp"
#include "Core/Log.hpp"
#include "Core/VFS.hpp" // 【新增】：引入 VFS 虚拟文件系统

#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <algorithm> // 用于 std::replace

namespace Ayaya {

    std::shared_ptr<TextureCube> TextureCube::Create(const std::vector<std::string>& faces) {
        
        // ==========================================
        // 【核心防御与 VFS 改造】：VFS 路径解析 + 路径清洗器
        // 1. 将传入的虚拟路径 (project:// 或 engine://) 解析为电脑上的绝对物理路径
        // 2. 自动将所有 Windows 风格的反斜杠 '\' 替换为跨平台通用的正斜杠 '/'
        // ==========================================
        std::vector<std::string> sanitizedFaces;
        sanitizedFaces.reserve(faces.size());

        for (const auto& path : faces) {
            // 【新增】：调用 VFS 拿到真实的硬盘物理路径
            std::string physicalPath = VFS::ResolveString(path);
            
            // 继续执行原有的路径清洗
            std::replace(physicalPath.begin(), physicalPath.end(), '\\', '/');
            
            sanitizedFaces.push_back(physicalPath);
        }

        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
            case RendererAPI::API::OpenGL:  
                // 此时传给底层 OpenGL 的，已经是彻彻底底的物理硬盘路径了！
                return std::make_shared<OpenGLTextureCube>(sanitizedFaces);
            case RendererAPI::API::Vulkan:  
                return std::make_shared<VulkanTextureCube>(sanitizedFaces);
            case RendererAPI::API::Metal:   
                AYAYA_CORE_ERROR("Metal TextureCube is under construction!"); 
                return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

    // =====================================================================
    // 【新增】：解析 .cube 单文件资产的工厂实现
    // =====================================================================
    std::shared_ptr<TextureCube> TextureCube::Create(const std::string& filepath) {
        // 1. 将传入的虚拟路径解析为真实的物理路径
        std::string physicalPath = VFS::ResolveString(filepath);
        std::filesystem::path path(physicalPath);

        if (path.extension() != ".cube") {
            AYAYA_CORE_ERROR("TextureCube: Invalid asset format! Expected .cube, got {0}", path.extension().string());
            return nullptr;
        }

        // ==========================================
        // 【核心修复 1】：防止 YAML::BadFile 导致引擎闪退！
        // 在交给 yaml-cpp 解析之前，先判断文件存不存在
        // ==========================================
        if (!std::filesystem::exists(path)) {
            AYAYA_CORE_ERROR("TextureCube: File not found at path: {0}", physicalPath);
            return nullptr;
        }

        // 2. 将 .cube 文件作为 YAML 读取
        YAML::Node data;
        try {
            data = YAML::LoadFile(physicalPath);
        } catch (const YAML::Exception& e) { 
            // 【核心修复 2】：扩大异常捕获范围，拦截所有 YAML 读取错误
            AYAYA_CORE_ERROR("TextureCube: Failed to parse .cube file '{0}'. Error: {1}", physicalPath, e.what());
            return nullptr;
        }

        auto cubemapNode = data["Cubemap"];
        if (!cubemapNode) {
            AYAYA_CORE_ERROR("TextureCube: Invalid .cube file! Missing 'Cubemap' root node.");
            return nullptr;
        }

        // 3. 智能路径解析闭包
        std::string baseDir = path.parent_path().string();
        auto ResolveFacePath = [&](const std::string& facePath) -> std::string {
            if (facePath.empty()) return "";
            if (facePath.find("://") != std::string::npos) return VFS::ResolveString(facePath); 
            return (std::filesystem::path(baseDir) / facePath).string(); 
        };

        // 4. 按 OpenGL 规定的严格顺序读取 6 个面
        std::vector<std::string> faces(6);
        faces[0] = ResolveFacePath(cubemapNode["Right"].as<std::string>(""));
        faces[1] = ResolveFacePath(cubemapNode["Left"].as<std::string>(""));
        faces[2] = ResolveFacePath(cubemapNode["Top"].as<std::string>(""));
        faces[3] = ResolveFacePath(cubemapNode["Bottom"].as<std::string>(""));
        faces[4] = ResolveFacePath(cubemapNode["Front"].as<std::string>(""));
        faces[5] = ResolveFacePath(cubemapNode["Back"].as<std::string>(""));

        // 5. 校验 6 张图是否全部存在
        for (int i = 0; i < 6; ++i) {
            if (faces[i].empty() || !std::filesystem::exists(faces[i])) {
                AYAYA_CORE_ERROR("TextureCube: Missing or invalid image path for face index {0} in {1}", i, physicalPath);
                return nullptr; 
            }
        }

        return Create(faces);
    }

    std::shared_ptr<TextureCube> TextureCube::Create(void* rendererID, int width, int height) {
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::None:    
                AYAYA_CORE_ERROR("RendererAPI::None is currently not supported!"); 
                return nullptr;
            case RendererAPI::API::OpenGL:  
                return std::make_shared<OpenGLTextureCube>(rendererID, width, height);
            case RendererAPI::API::Vulkan:  
                return std::make_shared<VulkanTextureCube>(rendererID, width, height);
            case RendererAPI::API::Metal:   
                AYAYA_CORE_ERROR("Metal TextureCube is under construction!"); 
                return nullptr;
        }
        AYAYA_CORE_ERROR("Unknown RendererAPI!");
        return nullptr;
    }

}