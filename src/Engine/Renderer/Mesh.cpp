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
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent"  } // <--- 新增
        });
        m_VertexArray->AddVertexBuffer(vbo);

        auto ibo = std::shared_ptr<IndexBuffer>(IndexBuffer::Create((uint32_t*)indices.data(), indices.size()));
        m_VertexArray->SetIndexBuffer(ibo);

        // ==========================================
        // 核心：计算本地 AABB
        // ==========================================
        for (const auto& vertex : vertices) {
            m_BoundingBox.Min.x = std::min(m_BoundingBox.Min.x, vertex.Position.x);
            m_BoundingBox.Min.y = std::min(m_BoundingBox.Min.y, vertex.Position.y);
            m_BoundingBox.Min.z = std::min(m_BoundingBox.Min.z, vertex.Position.z);

            m_BoundingBox.Max.x = std::max(m_BoundingBox.Max.x, vertex.Position.x);
            m_BoundingBox.Max.y = std::max(m_BoundingBox.Max.y, vertex.Position.y);
            m_BoundingBox.Max.z = std::max(m_BoundingBox.Max.z, vertex.Position.z);
        }
    }

    std::shared_ptr<Mesh> Mesh::CreateCube(float size) {
        float half = size / 2.0f;

        // 24 个顶点：现在每个顶点包含 Position, Normal, TexCoord, Tangent
        std::vector<Vertex> vertices = {
            // Front face (Z = +half) -> 法线 (0, 0, 1), 切线沿 +X 方向 (1, 0, 0)
            { {-half, -half,  half}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },
            { { half, -half,  half}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },
            { { half,  half,  half}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f} },
            { {-half,  half,  half}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f} },

            // Back face (Z = -half) -> 法线 (0, 0, -1), 切线沿 -X 方向 (-1, 0, 0)
            { { half, -half, -half}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f} },
            { {-half, -half, -half}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f} },
            { {-half,  half, -half}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f} },
            { { half,  half, -half}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f} },

            // Left face (X = -half) -> 法线 (-1, 0, 0), 切线沿 +Z 方向 (0, 0, 1)
            { {-half, -half, -half}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f} },
            { {-half, -half,  half}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f} },
            { {-half,  half,  half}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f} },
            { {-half,  half, -half}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f} },

            // Right face (X = +half) -> 法线 (1, 0, 0), 切线沿 -Z 方向 (0, 0, -1)
            { { half, -half,  half}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} },
            { { half, -half, -half}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f} },
            { { half,  half, -half}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f} },
            { { half,  half,  half}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f} },

            // Top face (Y = +half) -> 法线 (0, 1, 0), 切线沿 +X 方向 (1, 0, 0)
            { {-half,  half,  half}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },
            { { half,  half,  half}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },
            { { half,  half, -half}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f} },
            { {-half,  half, -half}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f} },

            // Bottom face (Y = -half) -> 法线 (0, -1, 0), 切线沿 +X 方向 (1, 0, 0)
            { {-half, -half, -half}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },
            { { half, -half, -half}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },
            { { half, -half,  half}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f} },
            { {-half, -half,  half}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f} }
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

    std::shared_ptr<Mesh> Mesh::CreatePlane(float width, float height) {
        float halfW = width / 2.0f;
        float halfH = height / 2.0f;

        std::vector<Vertex> vertices = {
            // Position                  // Normal             // UV         // Tangent (沿 +X 方向)
            { {-halfW, 0.0f, -halfH},    {0.0f, 1.0f, 0.0f},   {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f} }, // 左上
            { { halfW, 0.0f, -halfH},    {0.0f, 1.0f, 0.0f},   {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f} }, // 右上
            { { halfW, 0.0f,  halfH},    {0.0f, 1.0f, 0.0f},   {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f} }, // 右下
            { {-halfW, 0.0f,  halfH},    {0.0f, 1.0f, 0.0f},   {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} }  // 左下
        };

        std::vector<uint32_t> indices = {
            0, 1, 2,
            2, 3, 0
        };

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> Mesh::CreateSphere(float radius, uint32_t xSegments, uint32_t ySegments) {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        const float PI = 3.14159265359f;

        // 1. 生成所有顶点数据 (保持不变)
        for (uint32_t y = 0; y <= ySegments; ++y) {
            for (uint32_t x = 0; x <= xSegments; ++x) {
                float xSegment = (float)x / (float)xSegments;
                float ySegment = (float)y / (float)ySegments;

                // 极坐标转换
                float xPos = std::cos(xSegment * 2.0f * PI) * std::sin(ySegment * PI);
                float yPos = std::cos(ySegment * PI);
                float zPos = std::sin(xSegment * 2.0f * PI) * std::sin(ySegment * PI);

                Vertex vertex;
                vertex.Position = glm::vec3(xPos, yPos, zPos) * radius;
                vertex.Normal = glm::vec3(xPos, yPos, zPos);
                vertex.TexCoord = glm::vec2(xSegment, ySegment);

                vertex.Tangent = glm::vec3(
                    -std::sin(xSegment * 2.0f * PI),
                    0.0f,
                    std::cos(xSegment * 2.0f * PI)
                );
                
                if (glm::length(vertex.Tangent) < 0.0001f) {
                    vertex.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                } else {
                    vertex.Tangent = glm::normalize(vertex.Tangent);
                }

                vertices.push_back(vertex);
            }
        }

        // ==========================================
        // 2. 核心修复：按 GL_TRIANGLES 生成索引！
        // ==========================================
        for (uint32_t y = 0; y < ySegments; ++y) {
            for (uint32_t x = 0; x < xSegments; ++x) {
                // 计算当前四边形的四个顶点索引
                uint32_t i0 = y * (xSegments + 1) + x;       // 左上
                uint32_t i1 = i0 + 1;                        // 右上
                uint32_t i2 = (y + 1) * (xSegments + 1) + x; // 左下
                uint32_t i3 = i2 + 1;                        // 右下

                // ==========================================
                // 终极修复：逆时针 (CCW) 环绕顺序，确保法线与渲染面一致
                // ==========================================
                // 第一个三角形 (右上, 左上, 右下) -> 逆时针
                indices.push_back(i0);
                indices.push_back(i1);
                indices.push_back(i2);

                // 第二个三角形 (左上, 左下, 右下) -> 逆时针
                indices.push_back(i1);
                indices.push_back(i3);
                indices.push_back(i2);
            }
        }

        return std::make_shared<Mesh>(vertices, indices);
    }
}