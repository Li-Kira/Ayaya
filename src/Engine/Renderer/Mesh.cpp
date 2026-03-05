#include "ayapch.h"
#include "Mesh.hpp"

namespace Ayaya {

    Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
        m_VertexArray.reset(VertexArray::Create());

        // 将 Vertex 结构体数组转换为紧凑的 Buffer
        auto vbo = std::shared_ptr<VertexBuffer>(VertexBuffer::Create((float*)vertices.data(), vertices.size() * sizeof(Vertex)));
        
        // 注意这里的 Layout：位置(Float3)、法线(Float3)、UV(Float2)
        vbo->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal"   },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        m_VertexArray->AddVertexBuffer(vbo);

        auto ibo = std::shared_ptr<IndexBuffer>(IndexBuffer::Create((uint32_t*)indices.data(), indices.size()));
        m_VertexArray->SetIndexBuffer(ibo);
    }

    std::shared_ptr<Mesh> Mesh::CreateCube(float size) {
        float half = size / 2.0f;

        // 24 个顶点：6个面，每个面4个顶点，各自拥有独立的法线和UV
        std::vector<Vertex> vertices = {
            // Front face (Z = +half) -> 法线 (0, 0, 1)
            { {-half, -half,  half}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f} },
            { { half, -half,  half}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f} },
            { { half,  half,  half}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f} },
            { {-half,  half,  half}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },

            // Back face (Z = -half) -> 法线 (0, 0, -1)
            { { half, -half, -half}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f} },
            { {-half, -half, -half}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f} },
            { {-half,  half, -half}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f} },
            { { half,  half, -half}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f} },

            // Left face (X = -half) -> 法线 (-1, 0, 0)
            { {-half, -half, -half}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} },
            { {-half, -half,  half}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} },
            { {-half,  half,  half}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f} },
            { {-half,  half, -half}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f} },

            // Right face (X = +half) -> 法线 (1, 0, 0)
            { { half, -half,  half}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} },
            { { half, -half, -half}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} },
            { { half,  half, -half}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f} },
            { { half,  half,  half}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f} },

            // Top face (Y = +half) -> 法线 (0, 1, 0)
            { {-half,  half,  half}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
            { { half,  half,  half}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} },
            { { half,  half, -half}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} },
            { {-half,  half, -half}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} },

            // Bottom face (Y = -half) -> 法线 (0, -1, 0)
            { {-half, -half, -half}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f} },
            { { half, -half, -half}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f} },
            { { half, -half,  half}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f} },
            { {-half, -half,  half}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f} }
        };

        std::vector<uint32_t> indices = {
             0,  1,  2,  2,  3,  0,
             4,  5,  6,  6,  7,  4,
             8,  9, 10, 10, 11,  8,
            12, 13, 14, 14, 15, 12,
            16, 17, 18, 18, 19, 16,
            20, 21, 22, 22, 23, 20
        };

        return std::make_shared<Mesh>(vertices, indices);
    }
}