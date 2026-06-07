#pragma once

#include "Mesh.hpp"
#include "Asset/AssetSettings.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace Ayaya {

    // 模型的节点树结构：存储原始模型文件里的层级和局部变换
    struct ModelNode {
        std::string Name;
        glm::mat4 LocalTransform;
        std::vector<std::shared_ptr<Mesh>> Meshes; // 该节点自带的网格
        std::vector<ModelNode> Children;           // 子节点
    };

    class Model {
    public:
        Model(const std::string& path);
        Model(const std::string& path, const ModelImportSettings& settings);
        Model(const std::shared_ptr<Mesh>& mesh); // 兼容单网格创建

        const std::vector<std::shared_ptr<Mesh>>& GetMeshes() const { return m_Meshes; }
        const std::string& GetPath() const { return m_Path; }
        void SetPath(const std::string& path) { m_Path = path; }
        const ModelNode& GetRootNode() const { return m_RootNode; }

    private:
        void LoadModel(const std::string& path, const ModelImportSettings& settings);
        // 核心修复：递归处理节点层级
        ModelNode ProcessNode(aiNode* node, const aiScene* scene, const ModelImportSettings& settings,
                              bool isRoot = false);
        std::shared_ptr<Mesh> ProcessMesh(aiMesh* mesh, const aiScene* scene);

    private:
        std::vector<std::shared_ptr<Mesh>> m_Meshes; // 依然保留，用于简单的一键渲染
        std::string m_Directory;
        std::string m_Path;
        ModelNode m_RootNode; // 存储构建好的层级树
    };

}