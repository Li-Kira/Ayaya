#include <Ayaya.hpp>
#include "SandboxLayer.hpp"
#include "Renderer/Renderer.hpp"
#include "Core/VFS.hpp"
#include "Project/LaunchConfig.hpp"
#include <filesystem>

namespace Ayaya {

    // 导出游戏启动配置，由 main() 在 CreateApplication() 前填充。
    static LaunchConfig           s_LaunchConfig;
    static std::filesystem::path s_GameRoot;

    class SandboxApp : public Application {
    public:
        SandboxApp() : Application() {
            PushLayer(new SandboxLayer(s_LaunchConfig, s_GameRoot));
        }

        ~SandboxApp() {}
    };

    Application* CreateApplication() {
        return new SandboxApp();
    }
}

// 应用程序入口点
int main(int argc, char** argv) {
    using namespace Ayaya;

    // ==========================================
    // 1. 定位 game.json：argv[1] 优先，否则从 argv[0] 相邻目录发现
    //    (macOS bundle: Foo.app/Contents/Resources/game.json)
    // ==========================================
    std::filesystem::path configPath;
    if (argc > 1) {
        configPath = argv[1];
    } else {
        std::filesystem::path exeDir = std::filesystem::absolute(argv[0]).parent_path();
        std::filesystem::path candidates[] = {
            exeDir / "game.json",
            exeDir / "Resources" / "game.json",
            exeDir / ".." / "Resources" / "game.json",
        };
        for (auto& c : candidates) {
            if (std::filesystem::exists(c)) { configPath = c; break; }
        }
    }

    if (configPath.empty() || !LaunchConfig::Load(configPath, s_LaunchConfig)) {
        AYAYA_CORE_ERROR("[Sandbox] No valid game.json provided. Usage: Sandbox <path/to/game.json>");
        return 1;
    }

    s_GameRoot = configPath.parent_path();

    // 提前挂载 engine，使 ImGuiLayer 字体加载 (发生在 Application 构造、早于 SandboxLayer::OnAttach)
    // 能解析到 bundle 内的 engine://Editor/fonts/...
    VFS::Mount("engine", s_GameRoot / "engine");

    // ==========================================
    // 2. 在 Application() 构造前注入图形后端（否则会被 EditorPreferences.yaml 覆盖）
    // ==========================================
    if (s_LaunchConfig.GraphicsAPI == "OpenGL") {
        Renderer::SetGraphicsAPIOverride(RendererAPI::API::OpenGL);
    } else {
        Renderer::SetGraphicsAPIOverride(RendererAPI::API::Vulkan);
    }

    // ==========================================
    // 3. 运行
    // ==========================================
    Application* app = CreateApplication();
    app->Run();
    delete app;
    return 0;
}
