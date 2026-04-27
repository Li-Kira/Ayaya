#include "ayapch.h"
#include "Model.hpp"

namespace Ayaya {

    // 辅助函数：将 Assimp 的矩阵转换为 GLM 矩阵
    static glm::mat4 AssimpMatToGlm(const aiMatrix4x4& from) {
        glm::mat4 to;
        to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
        to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
        to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
        to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
        return to;
    }

    Model::Model(const std::string& path) {
        LoadModel(path);
    }

    Model::Model(const std::shared_ptr<Mesh>& mesh) {
        m_Meshes.push_back(mesh);
        m_RootNode.Name = "Raw Mesh";
        m_RootNode.LocalTransform = glm::mat4(1.0f);
        m_RootNode.Meshes.push_back(mesh);
    }

    void Model::LoadModel(const std::string& path) {
        m_Path = path;
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, 
            aiProcess_Triangulate | aiProcess_GenSmoothNormals | 
            aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            AYAYA_CORE_ERROR("Assimp Error: {0}", importer.GetErrorString());
            return;
        }

        m_Directory = path.substr(0, path.find_last_of('/'));
        // 从根节点递归
        m_RootNode = ProcessNode(scene->mRootNode, scene);
    }

    ModelNode Model::ProcessNode(aiNode* node, const aiScene* scene) {
        ModelNode modelNode;
        modelNode.Name = node->mName.C_Str();
        modelNode.LocalTransform = AssimpMatToGlm(node->mTransformation);

        // 处理当前节点的所有网格
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            auto processedMesh = ProcessMesh(mesh, scene);
            modelNode.Meshes.push_back(processedMesh);
            m_Meshes.push_back(processedMesh); // 兼容旧逻辑
        }

        // 递归处理子节点
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            modelNode.Children.push_back(ProcessNode(node->mChildren[i], scene));
        }

        return modelNode;
    }

    std::shared_ptr<Mesh> Model::ProcessMesh(aiMesh* mesh, const aiScene* scene) {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        // 1. 提取顶点数据 (位置、法线、UV)
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;
            
            // 位置
            vertex.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
            
            // 法线
            if (mesh->HasNormals()) {
                vertex.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
            } else {
                vertex.Normal = { 0.0f, 0.0f, 0.0f };
            }

            // UV 贴图坐标 (Assimp 允许一个顶点最多有 8 组 UV，我们只取第 0 组)
            if (mesh->mTextureCoords[0]) {
                vertex.TexCoord = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
            } else {
                vertex.TexCoord = { 0.0f, 0.0f };
            }

            // --- 提取切线 ---
            if (mesh->HasTangentsAndBitangents()) {
                vertex.Tangent = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
            } else {
                vertex.Tangent = { 0.0f, 0.0f, 0.0f }; // 兜底
            }

            vertices.push_back(vertex);
        }

        // 2. 提取索引数据 (每个面/三角形的顶点序号)
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j]);
            }
        }

        // 把提取出的纯数据交给我们的 Mesh 类生成 OpenGL 缓冲区！
        return std::make_shared<Mesh>(vertices, indices);
    }
}