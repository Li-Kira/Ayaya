#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector> // --- 新增：为了存储子节点列表 ---

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "Renderer/SceneCamera.hpp"

// --- 新增：引入 UUID ---
#include "Engine/Core/UUID.hpp"

namespace Ayaya {
    // 数据层

    // ==========================================
    // 新增：ID 组件 (用于序列化时的唯一身份识别)
    // ==========================================
    struct IDComponent {
        UUID ID;
        IDComponent() = default;
        IDComponent(const IDComponent&) = default;
        IDComponent(UUID id) : ID(id) {}
    };

    // 用来在大纲里显示名字
    struct TagComponent {
        std::string Tag;

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag) : Tag(tag) {}
    };

    // 用来记录位置、旋转、缩放
    struct TransformComponent {
        glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f }; // 欧拉角
        glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::vec3& translation) : Translation(translation) {}

        // 动态计算变换矩阵
        glm::mat4 GetTransform() const {
            glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));
            return glm::translate(glm::mat4(1.0f), Translation)
                * rotation
                * glm::scale(glm::mat4(1.0f), Scale);
        }
    };

    // 精灵渲染组件 (给物体上色、贴图)
    struct SpriteRendererComponent {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };

        // --- 新增：存储贴图的 UUID ---
        UUID TextureHandle = 0;

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(const glm::vec4& color) : Color(color) {}
    };

    // 相机组件 (纯数据，没有任何控制逻辑)
    struct CameraComponent {
        Ayaya::SceneCamera Camera;
        bool Primary = true;           // 标志这是否是当前负责渲染画面的主相机
        bool FixedAspectRatio = false; // 是否锁定长宽比（防止窗口缩放时画面被拉伸变形）

        CameraComponent() = default;
        CameraComponent(const CameraComponent&) = default;
    };

    // ==============================================
    // 新增：关系组件 (存储父子层级数据)
    // ==============================================
    struct RelationshipComponent {
        entt::entity Parent = entt::null;           // 指向父节点的 ID
        std::vector<entt::entity> Children;         // 存储所有子节点的 ID

        RelationshipComponent() = default;
        RelationshipComponent(const RelationshipComponent&) = default;
    };

}