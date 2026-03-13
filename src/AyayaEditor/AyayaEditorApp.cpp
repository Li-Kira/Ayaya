#include <Ayaya.hpp>
#include "EditorLayer.hpp"
#include <filesystem>

namespace Ayaya {

    class AyayaEditorApp : public Application {
    public:
        AyayaEditorApp() : Application() {
            // 将编辑器专属的 Layer 推入引擎
            PushLayer(new EditorLayer());
        }

        ~AyayaEditorApp() {}
    };

    Application* CreateApplication() {
        return new AyayaEditorApp();
    }
}

// 应用程序入口点
int main(int argc, char** argv) {
    // ==========================================
    // 核心修复：自动重定向工作目录 (Working Directory)
    // ==========================================
    std::filesystem::path exePath = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::current_path(exePath);
    
    Ayaya::Application* app = Ayaya::CreateApplication();
    app->Run();
    delete app;
    return 0;
}