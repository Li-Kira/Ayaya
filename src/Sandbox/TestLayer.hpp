#include <Ayaya.hpp>
#include <Renderer/Shader.hpp>
#include <glad/glad.h>

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