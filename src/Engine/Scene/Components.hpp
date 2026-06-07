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
    // 3D 网格渲染组件
    // ==========================================
    struct MeshRendererComponent {
        UUID ModelHandle = 0;     // 指向 AssetManager 里的 Model 资产
        UUID MaterialHandle = 0;  // 指向 AssetManager 里的 Material 资产

        bool CastShadows = true;     // 是否产生阴影
        bool ReceiveShadows = true;  // 是否接收阴影

        MeshRendererComponent() = default;
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
        
        // 资产引用全部替换为 64位 无符号整型 UUID
        UUID EquirectangularHandle = 0; 
        UUID CubemapHandle = 0; // 替代原来那个 std::vector<std::string> 
        
        float Intensity = 30000.0f; 
        float Lod = 0.0f;           
        bool IsDirty = false;       

        glm::vec3 AmbientColor = { 0.1f, 0.1f, 0.1f }; // 默认给一个微弱的暗灰色

        EnvironmentComponent() = default;
        EnvironmentComponent(const EnvironmentComponent&) = default;
    };

    // ==========================================
    // 后处理体积组件 (Post-Process Volume)
    // ==========================================
    struct PostProcessVolumeComponent {
        bool IsGlobal = true; // 是否为全局体积（未来可以扩展局部体积和混合权重）
        
        // --- 色调映射与曝光 ---
        int ToneMappingType = 0; // 0: None, 1: ACES, 2: Reinhard
        float Exposure = 1.0f;

        // --- Bloom ---
        bool EnableBloom = false;
        float BloomThreshold = 1.0f;
        float BloomKnee = 0.1f;
        float BloomRadius = 0.005f;
        float BloomIntensity = 1.0f;

        // --- FXAA ---
        bool EnableFXAA = false;

        // --- SSAO ---
        bool  EnableSSAO = false;
        float SSAORadius = 0.5f;
        float SSAOBias   = 0.025f;

        PostProcessVolumeComponent() = default;
        PostProcessVolumeComponent(const PostProcessVolumeComponent&) = default;
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
        UUID ScriptHandle = 0; // 脚本资产的唯一 ID

        // 仅在运行时有效，用于存储当前实体的专属 Lua 环境 (沙盒)
        void* RuntimeEnvironment = nullptr;

        // Persisted: user-modified CONFIG values (name → stringified value)
        std::unordered_map<std::string, std::string> ConfigOverrides;

        // Transient: last-known mtime of the .lua file for hot-reload detection
        uint64_t ScriptFileMtime = 0;

        LuaScriptComponent() = default;
        LuaScriptComponent(const LuaScriptComponent&) = default;
        LuaScriptComponent(UUID scriptHandle) : ScriptHandle(scriptHandle) {}
    };

    // ==========================================
    // UI 组件 (ECS-based UI System)
    // ==========================================

    // 替代 3D Transform，专用于 2D 屏幕空间的变换与布局
    struct RectTransformComponent {
        glm::vec2 AnchorMin{ 0.5f, 0.5f };
        glm::vec2 AnchorMax{ 0.5f, 0.5f };
        glm::vec2 Pivot{ 0.5f, 0.5f };
        glm::vec2 Position{ 0.0f, 0.0f };
        glm::vec2 Size{ 100.0f, 100.0f };
        float Rotation = 0.0f;
        glm::vec2 Scale{ 1.0f, 1.0f };

        // 运行时由 UILayoutSystem 计算的缓存值
        glm::vec2 CalculatedSize{ 0.0f, 0.0f };    // Anchor 拉伸后实际像素尺寸
        glm::mat4 HierarchyTransform{ 1.0f };       // 传给子节点的累积矩阵
        glm::mat4 RenderTransform{ 1.0f };          // 提交 GPU 渲染 Quad 的矩阵
        glm::vec2 ScreenMin{ 0.0f, 0.0f };         // 射线检测包围盒
        glm::vec2 ScreenMax{ 0.0f, 0.0f };
        bool LayoutDirty = true;

        RectTransformComponent() = default;
        RectTransformComponent(const RectTransformComponent&) = default;
    };

    // 画布属性 — 挂载在 UI 树的根节点
    struct CanvasComponent {
        enum class RenderMode { ScreenSpaceOverlay, ScreenSpaceCamera, WorldSpace };
        RenderMode Mode = RenderMode::ScreenSpaceOverlay;
        int SortOrder = 0;

        CanvasComponent() = default;
        CanvasComponent(const CanvasComponent&) = default;
    };

    // UI 图片渲染
    struct UIImageComponent {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        UUID TextureHandle = 0;

        UIImageComponent() = default;
        UIImageComponent(const UIImageComponent&) = default;
    };

    // UI 文本渲染
    struct UITextComponent {
        std::string Text;
        UUID FontHandle = 0;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float FontSize = 16.0f;

        UITextComponent() = default;
        UITextComponent(const UITextComponent&) = default;
    };

    // UI 按钮交互
    struct UIButtonComponent {
        enum class State { Normal, Hover, Pressed, Disabled };
        State CurrentState = State::Normal;

        glm::vec4 NormalColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 HoverColor{ 0.8f, 0.8f, 0.8f, 1.0f };
        glm::vec4 PressedColor{ 0.6f, 0.6f, 0.6f, 1.0f };

        std::string OnClickCallback;

        UIButtonComponent() = default;
        UIButtonComponent(const UIButtonComponent&) = default;
    };

}