#include <Ayaya.hpp>
#include "SandboxLayer.hpp"

namespace Ayaya {

    class SandboxApp : public Application {
    public:
        SandboxApp() : Application() {
            PushLayer(new SandboxLayer());
        }

        ~SandboxApp() {}
    };

    Application* CreateApplication() {
        return new SandboxApp();
    }
}

// 应用程序入口点
int main(int argc, char** argv) {
    Ayaya::Application* app = Ayaya::CreateApplication();
    app->Run();
    delete app;
    return 0;
}