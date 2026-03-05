#pragma once

#include "VertexArray.hpp"
#include "Buffer.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Ayaya {

    // ==========================================
    // 标准的 3D 顶点结构：位置、法线、UV
    // ==========================================
    struct Vertex {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;
    };

    class Mesh {
    public:
        // 通过传入顶点数组和索引数组来构建网格
        Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
        ~Mesh() = default;

        std::shared_ptr<VertexArray> GetVertexArray() const { return m_VertexArray; }

        // ==========================================
        // 几何体工厂：一键生成标准图元
        // ==========================================
        static std::shared_ptr<Mesh> CreateCube(float size = 1.0f);
        // 未来还可以加：CreateSphere, CreatePlane, CreateCylinder...

    private:
        std::shared_ptr<VertexArray> m_VertexArray;
    };

}