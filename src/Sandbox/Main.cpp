#include <Ayaya.hpp>

class TestLayer : public Ayaya::Layer {
public:
    TestLayer() : Layer("Test") {}

    void OnUpdate(Ayaya::Timestep ts) override {
        // 假设物体的速度是 5.0 单位/秒
        float speed = 5.0f;
        
        if (Ayaya::Input::IsKeyPressed(Ayaya::Key::D))
            m_ObjectX += speed * ts; // 这里的 ts 保证了位移是基于秒的，而不是基于帧的
            
        if (Ayaya::Input::IsKeyPressed(Ayaya::Key::A))
            m_ObjectX -= speed * ts;

        AYAYA_TRACE("Object Position: {0} (Delta: {1}ms)", m_ObjectX, ts.GetMilliseconds());
    }

private:
    float m_ObjectX = 0.0f;
};

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
    app.Run();
    return 0;
}