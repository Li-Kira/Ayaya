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
    // 打包成 .app 后资源在 Contents/Resources，CWD 指向那里 (所有 assets/ 相对路径都依赖此)
    // ==========================================
    std::filesystem::path exeDir = std::filesystem::absolute(argv[0]).parent_path(); // .../Contents/MacOS
    std::filesystem::path resources = exeDir / ".." / "Resources";
    std::filesystem::current_path(std::filesystem::exists(resources / "assets") ? resources : exeDir);

    Ayaya::Application* app = Ayaya::CreateApplication();
    app->Run();
    delete app;
    return 0;
}