#include <Ayaya.hpp>
#include <Renderer/Buffer.hpp>
#include <Renderer/VertexArray.hpp>

class ExampleLayer : public Ayaya::Layer {
public:
    ExampleLayer() : Layer("ExampleLayer") {}

    virtual void OnAttach() override {
        AYAYA_CORE_INFO("ExampleLayer Attached: Setting up Square...");

        // 1. 创建 VAO (顶点数组)
        m_VertexArray.reset(Ayaya::VertexArray::Create());

        // 2. 创建 VBO (顶点缓冲区)
        // 矩形的 4 个顶点: X, Y, Z
        float vertices[4 * 3] = {
            -0.5f, -0.5f, 0.0f,  // 0: 左下
             0.5f, -0.5f, 0.0f,  // 1: 右下
             0.5f,  0.5f, 0.0f,  // 2: 右上
            -0.5f,  0.5f, 0.0f   // 3: 左上
        };

        // 注意：这里使用智能指针管理
        std::shared_ptr<Ayaya::VertexBuffer> vbo;
        vbo.reset(Ayaya::VertexBuffer::Create(vertices, sizeof(vertices)));

        // 3. 设置布局 (Layout)
        // 这会自动计算 Stride 和 Offset
        vbo->SetLayout({
            { Ayaya::ShaderDataType::Float3, "a_Position" }
        });

        // 将 VBO 加入 VAO
        m_VertexArray->AddVertexBuffer(vbo);

        // 4. 创建 IBO (索引缓冲区)
        // 两个三角形拼成一个矩形
        uint32_t indices[6] = { 
            0, 1, 2, // 第一个三角形
            2, 3, 0  // 第二个三角形
        };
        std::shared_ptr<Ayaya::IndexBuffer> ibo;
        ibo.reset(Ayaya::IndexBuffer::Create(indices, 6));

        // 将 IBO 加入 VAO
        m_VertexArray->SetIndexBuffer(ibo);

        // 5. 加载 Shader
        m_Shader = std::make_unique<Ayaya::Shader>("assets/shaders/default.vert", "assets/shaders/default.frag");
    }

    virtual void OnUpdate(Ayaya::Timestep ts) override {
        float time = (float)glfwGetTime();
        float red = sin(time) * 0.5f + 0.5f;
        float green = cos(time) * 0.5f + 0.5f;

        // 渲染背景
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 使用封装后的 API 进行渲染
        m_Shader->Bind();
        // 通过 Uniform 动态更新颜色
        m_Shader->SetFloat4("u_Color", { red, green, 0.8f, 1.0f });

        m_VertexArray->Bind();
        glDrawElements(GL_TRIANGLES, m_VertexArray->GetIndexBuffer()->GetCount(), GL_UNSIGNED_INT, nullptr);

        m_VertexArray->Unbind();
        m_Shader->Unbind();
    }

private:
    std::unique_ptr<Ayaya::Shader> m_Shader;
    std::shared_ptr<Ayaya::VertexArray> m_VertexArray;
};