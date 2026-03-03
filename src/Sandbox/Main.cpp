#include <Core/Application.hpp>
#include "ExampleLayer.hpp"

class SandboxLayer : public Ayaya::Layer {
public:
    SandboxLayer() : Layer("SandboxLayer") {}

    void OnUpdate(Ayaya::Timestep ts) override {
        if (Ayaya::Input::IsKeyPressed(Ayaya::Key::Space)) {
            AYAYA_INFO("Space is pressed! Frame time: {0}ms", ts.GetMilliseconds());
        }
    }
};

int main() {
    Ayaya::Application app;
    // app.PushLayer(new SandboxLayer());
    // app.PushLayer(new TestLayer());
    // 推入测试Shader层
    app.PushLayer(new ExampleLayer());
    app.Run();
    return 0;
}