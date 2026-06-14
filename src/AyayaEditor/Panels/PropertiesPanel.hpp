// =========================================================================
// PropertiesPanel (属性检查器面板)
//
// 功能清单 (Feature List):
// 1. 多选编辑架构 (Multi-selection Editing)：支持同时选中多个实体，并批量修改它们共有的组件属性。
// 2. 组件生命周期管理：通过 "Add Component" 按钮添加组件，通过右键菜单/齿轮图标移除组件（支持多选批量移除）。
// 3. 撤销重做数据捕获 (Undo/Redo)：在 UI 控件编辑释放时，自动生成 ChangeComponentCommand + MacroCommand 录入历史。
// 4. 资产拖拽绑定 (Asset Drag & Drop)：支持从 ContentBrowser 接受 UUID Payload，绑定模型、贴图、材质、脚本、HDR 等。
// 5. 内联材质编辑器 (Inline Material Editor)：在 MeshRenderer 下展开材质球，直接修改 Albedo、Normal 等贴图和数值，修改后自动保存。
// 6. 物理与脚本支持：支持 Rigidbody2D, BoxCollider2D, LuaScriptComponent 的 UI 绘制。
// 7. 防崩溃内存保护：利用 m_TextureGarbageBin 延迟释放被替换的底层 Vulkan/OpenGL 贴图资源。
// =========================================================================

#pragma once

#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "UI_Widgets.hpp"
#include "Renderer/Texture.hpp"
#include <memory>
#include <vector>

namespace Ayaya {

    class PropertiesPanel {
    public:
        PropertiesPanel() = default;
        PropertiesPanel(const std::shared_ptr<Scene>& context);

        void SetContext(const std::shared_ptr<Scene>& context);
        void SetSelectedEntities(const std::vector<Entity>& selectedEntities);

        // 切换为资产检查模式（ContentBrowser 点击文件时调用）
        void SetSelectedAsset(UUID assetHandle) {
            if (m_Locked) return;
            m_SelectedAsset = assetHandle; m_SelectedEntities.clear();
        }

        // Inspector padlock
        bool IsLocked() const { return m_Locked; }

        void OnImGuiRender();

    private:
        void DrawComponents(Entity entity);
        void DrawTagComponent(Entity referenceEntity, float uiScale);
        void DrawTransformComponent(Entity entity);
        void DrawCameraComponent(Entity entity);
        void DrawSpriteRendererComponent(Entity entity, float uiScale);
        void DrawDirectionalLightComponent(Entity referenceEntity);
        void DrawPointLightComponent(Entity referenceEntity);
        void DrawEnvironmentComponent(Entity referenceEntity);
        void DrawMeshRendererComponent(Entity referenceEntity, float uiScale);
        void DrawPostProcessVolumeComponent(Entity referenceEntity, float uiScale);
        void DrawLuaScriptComponent(Entity referenceEntity);
        void DrawRigidbody2DComponent(Entity referenceEntity);
        void DrawBoxCollider2DComponent(Entity referenceEntity);
        void DrawCanvasComponent(Entity referenceEntity);
        void DrawRectTransformComponent(Entity referenceEntity, float uiScale);
        void DrawUIImageComponent(Entity referenceEntity, float uiScale);
        void DrawUITextComponent(Entity referenceEntity);
        void DrawUIButtonComponent(Entity referenceEntity);
        void DrawAnimationControllerComponent(Entity referenceEntity);
        void DrawAddComponentButton(Entity referenceEntity, float uiScale);
        void DrawAssetInspector();

    private:
        std::shared_ptr<Scene> m_Context;
        std::vector<Entity> m_SelectedEntities;
        UUID m_SelectedAsset = 0;
        bool m_Locked = false;

        // 【核心防御】：纹理垃圾桶，用于延迟析构被替换的贴图，防止 Vulkan 崩溃
        std::vector<std::shared_ptr<Texture2D>> m_TextureGarbageBin;
    };

}
