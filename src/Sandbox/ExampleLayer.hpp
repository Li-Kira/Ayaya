#include <Ayaya.hpp>
#include <Renderer/Shader.hpp>
#include <glad/glad.h>

class ExampleLayer : public Ayaya::Layer {
public:
    ExampleLayer() : Layer("ExampleLayer") {}

    // 当 Layer 被推入 LayerStack 时执行初始化
    virtual void OnAttach() override {
        AYAYA_CORE_INFO("ExampleLayer Attached");

        // 1. 创建 Shader
        // 注意：路径相对于 build 目录下的 assets 符号链接
        m_Shader = std::make_unique<Ayaya::Shader>("assets/shaders/default.vert", "assets/shaders/default.frag");

        // 2. 定义三角形顶点数据 (X, Y, Z)
        float vertices[] = {
            -0.5f, -0.5f, 0.0f, // 左下
             0.5f, -0.5f, 0.0f, // 右下
             0.0f,  0.5f, 0.0f  // 顶部
        };

        // 3. 创建并绑定顶点数组对象 (VAO)
        // VAO 会记录 VBO 的布局信息
        glGenVertexArrays(1, &m_VertexArray);
        glBindVertexArray(m_VertexArray);

        // 4. 创建并绑定顶点缓冲区对象 (VBO)
        glGenBuffers(1, &m_VertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
        // 将数据上传至显存
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        // 5. 设置顶点属性指针 (告诉 OpenGL 如何解释数据)
        // 对应 Shader 中的 layout(location = 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        // 解绑，防止后续意外修改
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    virtual void OnDetach() override {
        glDeleteVertexArrays(1, &m_VertexArray);
        glDeleteBuffers(1, &m_VertexBuffer);
    }

    virtual void OnUpdate(Ayaya::Timestep ts) override {
        // 每帧检测输入
        if (Ayaya::Input::IsKeyPressed(Ayaya::Key::Escape)) {
            // 这里可以通过 Application::Get().Close() 来关闭，目前先打日志
            AYAYA_INFO("ESC pressed in Layer!");
        }

        // 渲染逻辑
        m_Shader->Bind();
        glBindVertexArray(m_VertexArray);
        
        // 执行绘制：画 3 个顶点
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindVertexArray(0);
        m_Shader->Unbind();
    }

private:
    std::unique_ptr<Ayaya::Shader> m_Shader;
    uint32_t m_VertexArray = 0;
    uint32_t m_VertexBuffer = 0;
};