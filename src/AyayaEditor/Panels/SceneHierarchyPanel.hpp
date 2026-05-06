#pragma once

#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "UI_Widgets.hpp"
#include <memory>
#include <vector>
#include <algorithm>

namespace Ayaya {

    class SceneHierarchyPanel {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(const std::shared_ptr<Scene>& context);

        void SetContext(const std::shared_ptr<Scene>& context);
        void OnImGuiRender();

        // ==========================================
        // 核心修改：多选接口
        // ==========================================
        // 兼容旧代码：返回最后选中的“主实体”供 Gizmo 和 EditorLayer 使用
        Entity GetSelectedEntity() const { return m_SelectedEntities.empty() ? Entity{} : m_SelectedEntities.back(); }
        
        // 获取所有选中的实体
        const std::vector<Entity>& GetSelectedEntities() const { return m_SelectedEntities; }

        // 单选（清空其他）
        void SetSelectedEntity(Entity entity) {
            m_SelectedEntities.clear();
            if (entity) m_SelectedEntities.push_back(entity);
        }

        // 切换选择（Ctrl + 点击）
        void ToggleEntitySelection(Entity entity) {
            auto it = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity);
            if (it != m_SelectedEntities.end()) {
                m_SelectedEntities.erase(it); // 已经选中了则取消
            } else {
                m_SelectedEntities.push_back(entity); // 未选中则追加
            }
        }

        // 判断是否被选中
        bool IsEntitySelected(Entity entity) const {
            return std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity) != m_SelectedEntities.end();
        }

        void ClearSelection() { m_SelectedEntities.clear(); }

    private:
        void DrawEntityNode(Entity entity);
        void DrawComponents();

        void DrawTagComponent(Entity referenceEntity);
        void DrawTransformComponent(Entity referenceEntity);
        void DrawSpriteRendererComponent(Entity referenceEntity, float uiScale);
        void DrawCameraComponent(Entity referenceEntity);
        void DrawDirectionalLightComponent(Entity referenceEntity);
        void DrawPointLightComponent(Entity referenceEntity);
        void DrawEnvironmentComponent(Entity referenceEntity);
        void DrawMeshRendererComponent(Entity referenceEntity, float uiScale);
        void DrawPostProcessVolumeComponent(Entity referenceEntity, float uiScale);
        void DrawLuaScriptComponent(Entity referenceEntity);
        void DrawRigidbody2DComponent(Entity referenceEntity);
        void DrawBoxCollider2DComponent(Entity referenceEntity);
        void DrawAddComponentButton(Entity referenceEntity, float uiScale);

    private:
        std::shared_ptr<Scene> m_Context;
        
        // 多选列表
        std::vector<Entity> m_SelectedEntities;

        // ==========================================
        // 新增：用于支持 Shift 范围多选的辅助变量
        // ==========================================
        std::vector<Entity> m_VisibleNodes; // 当前帧所有“未被折叠隐藏”的节点顺序
        Entity m_LastClickedEntity = {};    // 记录上一次被选中的“锚点”物体
        Entity m_ShiftClickTarget = {};     // 记录当前被 Shift 点击的目标

        //将延迟操作的标记升级为数组，支持批量操作
        std::vector<Entity> m_EntitiesToDestroy;  
        std::vector<Entity> m_EntitiesToUnparent; 
        std::vector<Entity> m_EntitiesToDuplicate;

        // ==========================================
        // 【核心防御】：纹理垃圾桶，用于延迟析构被替换的贴图，防止 Vulkan 崩溃
        // ==========================================
        std::vector<std::shared_ptr<Texture2D>> m_TextureGarbageBin;
    };

}