#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector> // --- 新增：为了存储子节点列表 ---

#include <entt/entt.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "Renderer/SceneCamera.hpp"
#include "Renderer/Model.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/TextureCube.hpp"
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

        bool IsActive = true;

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
    // 相机组件 (纯数据，没有任何控制逻辑)
    struct CameraComponent {
        Ayaya::SceneCamera Camera;
        bool Primary = true;           // 标志这是否是当前负责渲染画面的主相机
        bool FixedAspectRatio = false; // 是否锁定长宽比（防止窗口缩放时画面被拉伸变形）
        float EV100 = 14.5f;

        // ==========================================
        // 核心完善：相机背景渲染模式
        // ==========================================
        enum class ClearFlags { Skybox, SolidColor };
        ClearFlags ClearFlag = ClearFlags::Skybox;                // 默认渲染天空盒
        glm::vec4 BackgroundColor = { 0.016f, 0.016f, 0.02f, 1.0f };

        CameraComponent() = default;
        CameraComponent(const CameraComponent&) = default;
    };

    struct RelationshipComponent {
        entt::entity Parent = entt::null;           // 指向父节点的 ID
        std::vector<entt::entity> Children;         // 存储所有子节点的 ID

        RelationshipComponent() = default;
        RelationshipComponent(const RelationshipComponent&) = default;
    };

    // ==========================================
    // 新增：3D 网格渲染组件
    // ==========================================
    struct MeshRendererComponent {
        std::shared_ptr<Model> ModelAsset; 
        
        // ==========================================
        // 升级：组件不再直接持有颜色，而是持有一个材质资产！
        // ==========================================
        std::shared_ptr<Material> MaterialAsset;

        bool CastShadows = false;     // 是否产生阴影
        bool ReceiveShadows = false;  // 是否接收阴影

        MeshRendererComponent() {
            // 默认依然给它塞一个我们引擎自带的 1x1 正方体，确保不会空指针
            ModelAsset = std::make_shared<Model>(Mesh::CreateCube(1.0f));
            MaterialAsset = std::make_shared<Material>();
        }
        MeshRendererComponent(const MeshRendererComponent&) = default;
    };

    // ==========================================
    // 新增：平行光组件 (太阳光)
    // ==========================================
    struct DirectionalLightComponent {
        glm::vec3 Color{ 1.0f, 1.0f, 1.0f }; // 默认纯白光
        float Illuminance = 100000.0f; 

        DirectionalLightComponent() = default;
        DirectionalLightComponent(const DirectionalLightComponent&) = default;
    };

    struct PointLightComponent {
        glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
        // 替换原来的 Intensity，改用光通量 (Lumens)，默认 1500 (家用灯泡)
        float LuminousPower = 1500.0f;

        float Radius = 10.0f;          // 强制衰减半径 (米)
        float Falloff = 1.0f;          // 衰减指数 (控制边缘平滑度)

        PointLightComponent() = default;
        PointLightComponent(const PointLightComponent&) = default;
    };

    // ==========================================
    // 新增：环境光 / 天空盒组件
    // ==========================================
    enum class EnvironmentType {
        None,
        HDR_Equirectangular, // .hdr 全景图
        LDR_Equirectangular, // .jpg/.png 全景图
        Classic_Cubemap      // 传统的 6 张图拼接天空盒
    };

    struct EnvironmentComponent {
        EnvironmentType Type = EnvironmentType::HDR_Equirectangular;
        
        std::string EquirectangularPath = ""; 
        std::vector<std::string> CubemapFaces = {"", "", "", "", "", ""};
        
        std::shared_ptr<Texture2D> EquirectangularTexture = nullptr;
        std::shared_ptr<TextureCube> ClassicCubemapTexture = nullptr;
        
        float Intensity = 30000.0f; 
        float Lod = 0.0f;           
        bool IsDirty = false;       

        // ==========================================
        // 【新增】：当没有天空盒，或需要额外补光时的基础环境光颜色
        // ==========================================
        glm::vec3 AmbientColor = { 0.1f, 0.1f, 0.1f }; // 默认给一个微弱的暗灰色

        EnvironmentComponent() = default;
        EnvironmentComponent(const EnvironmentComponent&) = default;
    };

    // ==========================================
    // Box2D 物理组件
    // ==========================================
    struct Rigidbody2DComponent {
        enum class BodyType { Static = 0, Dynamic = 1, Kinematic = 2 };
        BodyType Type = BodyType::Static;
        bool FixedRotation = false;

        // 核心：用于在运行时存储 Box2D 的底层指针 (b2Body*)
        // 使用 void* 是为了不污染 Components.hpp 的头文件依赖
        void* RuntimeBody = nullptr;

        Rigidbody2DComponent() = default;
        Rigidbody2DComponent(const Rigidbody2DComponent&) = default;
    };

    struct BoxCollider2DComponent {
        glm::vec2 Offset = { 0.0f, 0.0f };
        glm::vec2 Size = { 0.5f, 0.5f }; // Box2D 这里的 Size 代表的是半宽/半高 (Half-Extents)

        // 物理材质属性
        float Density = 1.0f;     // 密度 (决定质量)
        float Friction = 0.5f;    // 摩擦力
        float Restitution = 0.0f; // 弹力 (0=不弹, 1=完美反弹)
        float RestitutionThreshold = 0.5f; // 速度低于此值时不发生反弹

        void* RuntimeFixture = nullptr; // 存储底层 b2Fixture*

        BoxCollider2DComponent() = default;
        BoxCollider2DComponent(const BoxCollider2DComponent&) = default;
    };

    // ==========================================
    // Lua 脚本组件
    // ==========================================
    struct LuaScriptComponent {
        std::string ScriptPath = ""; 

        // 仅在运行时有效，用于存储当前实体的专属 Lua 环境 (沙盒)
        void* RuntimeEnvironment = nullptr; 

        LuaScriptComponent() = default;
        LuaScriptComponent(const LuaScriptComponent&) = default;
        LuaScriptComponent(const std::string& path) : ScriptPath(path) {}
    };

}