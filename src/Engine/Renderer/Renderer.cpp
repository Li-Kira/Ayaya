#include "ayapch.h"
#include "Renderer.hpp"
#include "Platform/OpenGL/OpenGLRendererAPI.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include <yaml-cpp/yaml.h>
#include <filesystem>

namespace Ayaya {

    // ==========================================
    // 【核心分配】：在这里初始化 RendererAPI 的静态变量，免去新建 cpp 文件的烦恼
    // ==========================================
    RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;
    
    std::unique_ptr<RendererAPI> Renderer::s_RendererAPI = nullptr;

    void Renderer::LoadConfig() {
        std::string prefsPath = "assets/Editor/settings/EditorPreferences.yaml";
        if (std::filesystem::exists(prefsPath)) {
            try {
                YAML::Node data = YAML::LoadFile(prefsPath);
                auto prefs = data["EditorPreferences"];
                if (prefs && prefs["GraphicsAPI"]) {
                    // 使用 RendererAPI::SetAPI 替代原来的 s_API 赋值
                    RendererAPI::SetAPI((prefs["GraphicsAPI"].as<int>() == 1) ? RendererAPI::API::Vulkan : RendererAPI::API::OpenGL);
                    AYAYA_CORE_INFO("Renderer Config: Selected {0} API", RendererAPI::GetAPI() == RendererAPI::API::Vulkan ? "Vulkan" : "OpenGL");
                    return;
                }
            } catch (const YAML::Exception& e) {
                AYAYA_CORE_ERROR("Failed to load early config: {0}", e.what());
            }
        }
        RendererAPI::SetAPI(RendererAPI::API::OpenGL); // Fallback
    }

    void Renderer::Init() {
        // 使用 RendererAPI::GetAPI() 进行判断
        switch (RendererAPI::GetAPI()) {
            case RendererAPI::API::OpenGL: s_RendererAPI = std::make_unique<OpenGLRendererAPI>(); break;
            case RendererAPI::API::Vulkan: AYAYA_CORE_ERROR("VulkanRendererAPI under construction!"); break;
            default: AYAYA_CORE_ERROR("Unknown API!"); break;
        }

        if (s_RendererAPI) {
            s_RendererAPI->Init();
        }
    }

    void Renderer::Shutdown() {
        s_RendererAPI.reset();
    }

    void Renderer::OnWindowResize(uint32_t width, uint32_t height) {
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            auto context = Application::Get().GetWindow().GetContext();
            auto vulkanContext = std::dynamic_pointer_cast<VulkanContext>(context);
            if (vulkanContext) {
                vulkanContext->RecreateSwapChain();
            }
        } 
        else if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            if (s_RendererAPI) {
                s_RendererAPI->SetViewport(0, 0, width, height);
            }
        }
    }
}