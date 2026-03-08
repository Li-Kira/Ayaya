#include "ayapch.h"
#include "Model.hpp"

namespace Ayaya {

    Model::Model(const std::string& path) {
        LoadModel(path);
    }

    Model::Model(const std::shared_ptr<Mesh>& mesh) {
        m_Meshes.push_back(mesh);
    }

    void Model::LoadModel(const std::string& path) {
        m_Path = path; // <--- 新增：记录下自己是从哪里加载的
        
        Assimp::Importer importer;
        // 开启强大的后处理魔法：自动将多边形转为三角形、翻转 UV 的 Y 轴、自动计算缺失的法线
        const aiScene* scene = importer.ReadFile(path, 
            aiProcess_Triangulate | 
            aiProcess_GenSmoothNormals | 
            aiProcess_FlipUVs | 
            aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            AYAYA_CORE_ERROR("ERROR::ASSIMP:: {0}", importer.GetErrorString());
            return;
        }

        // 提取目录路径 (例如 "assets/models/nanosuit.obj" -> "assets/models")
        m_Directory = path.substr(0, path.find_last_of('/'));

        // 从根节点开始递归处理所有子节点
        ProcessNode(scene->mRootNode, scene);
    }

    void Model::ProcessNode(aiNode* node, const aiScene* scene) {
        // 处理当前节点挂载的所有网格
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            m_Meshes.push_back(ProcessMesh(mesh, scene));
        }
        // 递归处理所有子节点
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            ProcessNode(node->mChildren[i], scene);
        }
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