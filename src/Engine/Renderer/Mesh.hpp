#pragma once

#include "VertexArray.hpp"
#include "Buffer.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Ayaya {

    // ==========================================
    // 3D 顶点结构：位置、法线、UV、切线，通过 pack 锁死 44 字节
    // ==========================================
#pragma pack(push, 1)
    struct Vertex {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;

        // ==========================================
        // 新增：切线向量，用于法线贴图的 TBN 矩阵计算
        // ==========================================
        glm::vec3 Tangent;
    };
#pragma pack(pop)

    struct AABB {
        glm::vec3 Min = {  100000.0f,  100000.0f,  100000.0f };
        glm::vec3 Max = { -100000.0f, -100000.0f, -100000.0f };
    };
    
    class Mesh {
    public:
        Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
             int materialIndex = -1);
        ~Mesh() = default;

        const AABB& GetAABB() const { return m_BoundingBox; }
        int GetMaterialIndex() const { return m_MaterialIndex; }
        void SetMaterialIndex(int idx) { m_MaterialIndex = idx; }

        uint32_t GetVertexCount() const { return m_VertexCount; }
        uint32_t GetIndexCount() const { return m_IndexCount; }

        // 【兼容】：供 OpenGL 老代码使用的无缝桥梁
        std::shared_ptr<VertexArray> GetVertexArray() const { return m_VertexArray; }

        // ==========================================
        // RHI 核心：只暴露底层的跨平台显存缓冲，不带任何 OpenGL 状态
        // ==========================================
        std::shared_ptr<VertexBuffer> GetVertexBuffer() const { return m_VertexBuffer; }
        std::shared_ptr<IndexBuffer> GetIndexBuffer() const { return m_IndexBuffer; }

        static std::shared_ptr<Mesh> CreateCube(float size = 1.0f);
        static std::shared_ptr<Mesh> CreatePlane(float width = 1.0f, float height = 1.0f);
        static std::shared_ptr<Mesh> CreateSphere(float radius = 1.0f, uint32_t xSegments = 32, uint32_t ySegments = 32);
        static std::shared_ptr<Mesh> Merge(const std::vector<std::shared_ptr<Mesh>>& meshes);

    private:
        uint32_t m_VertexCount = 0;
        uint32_t m_IndexCount = 0;
        int m_MaterialIndex = -1;

        AABB m_BoundingBox;

        std::shared_ptr<VertexArray> m_VertexArray;
        std::shared_ptr<VertexBuffer> m_VertexBuffer;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };

}