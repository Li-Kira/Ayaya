#pragma once

#include "VertexArray.hpp"
#include "Buffer.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Ayaya {

    // ==========================================
    // 3D 顶点结构：位置、法线、UV、切线
    // ==========================================
    struct Vertex {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;

        // ==========================================
        // 新增：切线向量，用于法线贴图的 TBN 矩阵计算
        // ==========================================
        glm::vec3 Tangent;
    };

    struct AABB {
        glm::vec3 Min = {  100000.0f,  100000.0f,  100000.0f };
        glm::vec3 Max = { -100000.0f, -100000.0f, -100000.0f };
    };
    
    class Mesh {
    public:
        // 通过传入顶点数组和索引数组来构建网格
        Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
        ~Mesh() = default;
        

        const AABB& GetAABB() const { return m_BoundingBox; }

        std::shared_ptr<VertexArray> GetVertexArray() const { return m_VertexArray; }

        // ==========================================
        // 几何体工厂：一键生成标准图元
        // ==========================================
        static std::shared_ptr<Mesh> CreateCube(float size = 1.0f);
        static std::shared_ptr<Mesh> CreatePlane(float width = 1.0f, float height = 1.0f);
        static std::shared_ptr<Mesh> CreateSphere(float radius = 0.5f, uint32_t xSegments = 64, uint32_t ySegments = 64);

    private:
        std::shared_ptr<VertexArray> m_VertexArray;
        AABB m_BoundingBox;
    };

}