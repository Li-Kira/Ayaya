#pragma once

#include "Scene.hpp"
#include <memory>
#include <string>

namespace Ayaya {

    class SceneSerializer {
    public:
        SceneSerializer(const std::shared_ptr<Scene>& scene);

        // 将当前场景保存为 YAML 文本文件
        void Serialize(const std::string& filepath);

        // 从 YAML 文本文件反序列化到当前场景
        bool Deserialize(const std::string& filepath);

    private:
        std::shared_ptr<Scene> m_Scene;
    };

}