#pragma once

#include "Scene.hpp"
#include "Renderer/MaterialSerializer.hpp"
#include <memory>
#include <string>
// 新增头文件用于类型推导
#include <type_traits>

namespace Ayaya {

    // ==========================================
    // 新增：编辑器状态数据包
    // ==========================================
    struct EditorState {
        bool ShowGrid = true;
        bool ShowSkybox = true;
        bool EnableMSAA = true;
        
        glm::vec3 CameraPosition = {0.0f, 0.0f, 0.0f};
        float CameraDistance = 10.0f;
        float CameraPitch = 0.0f;
        float CameraYaw = 0.0f;
        glm::vec3 CameraFocalPoint = {0.0f, 0.0f, 0.0f};

        // =======================================================
        // 【智能核心】：轻量级反射机制 (Visitor Pattern)
        // 把所有的变量名字和变量本身绑定在一起，供外部统一遍历
        // =======================================================
        template <typename Action>
        void ForEach(Action&& action) {
            action("ShowGrid", ShowGrid);
            action("ShowSkybox", ShowSkybox);
            action("EnableMSAA", EnableMSAA);
            action("CameraPosition", CameraPosition);
            action("CameraDistance", CameraDistance);
            action("CameraPitch", CameraPitch);
            action("CameraYaw", CameraYaw);
            action("CameraFocalPoint", CameraFocalPoint);
            // 以后新增了字段，只要在这里加一行 action("字段名", 字段); 即可
        }

        // 提供给 Serialize 保存使用的只读 (const) 版本
        template <typename Action>
        void ForEach(Action&& action) const {
            action("ShowGrid", ShowGrid);
            action("ShowSkybox", ShowSkybox);
            action("EnableMSAA", EnableMSAA);
            action("CameraPosition", CameraPosition);
            action("CameraDistance", CameraDistance);
            action("CameraPitch", CameraPitch);
            action("CameraYaw", CameraYaw);
            action("CameraFocalPoint", CameraFocalPoint);
        }
    };

    class SceneSerializer {
    public:
        SceneSerializer(const std::shared_ptr<Scene>& scene);

        // 将当前场景保存为 YAML 文本文件
        void Serialize(const std::string& filepath, const EditorState& editorState);

        // 从 YAML 文本文件反序列化到当前场景
        bool Deserialize(const std::string& filepath, EditorState& outEditorState);

    private:
        std::shared_ptr<Scene> m_Scene;
    };

}