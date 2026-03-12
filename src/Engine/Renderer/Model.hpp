#pragma once

#include "Mesh.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <string>
#include <vector>
#include <memory>

namespace Ayaya {

    class Model {
    public:
        // 从硬盘加载外部 3D 模型文件
        Model(const std::string& path);
        
        // 兼容原有的内置几何体（比如我们手捏的正方体）
        Model(const std::shared_ptr<Mesh>& mesh);

        const std::vector<std::shared_ptr<Mesh>>& GetMeshes() const { return m_Meshes; }

        // 新增：获取模型文件路径
        const std::string& GetPath() const { return m_Path; }

        // ==========================================
        // 新增：允许手动为内存生成的模型分配虚拟路径
        // ==========================================
        void SetPath(const std::string& path) { m_Path = path; }

    private:
        void LoadModel(const std::string& path);
        void ProcessNode(aiNode* node, const aiScene* scene);
        std::shared_ptr<Mesh> ProcessMesh(aiMesh* mesh, const aiScene* scene);

    private:
        std::vector<std::shared_ptr<Mesh>> m_Meshes;
        std::string m_Directory; // 保存模型所在的文件夹路径，方便以后加载同目录的贴图
        // 新增：保存模型的完整路径
        std::string m_Path;
    };

}