#include <Ayaya.hpp>
#include "EditorLayer.hpp"

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
    Ayaya::Application* app = Ayaya::CreateApplication();
    app->Run();
    delete app;
    return 0;
}