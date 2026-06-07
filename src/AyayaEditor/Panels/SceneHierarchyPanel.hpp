// =========================================================================
// SceneHierarchyPanel (场景层级树面板)
//
// 功能清单 (Feature List):
// 1. 场景树状视图渲染：递归遍历并显示场景中所有的实体（Entity）。
// 2. 核心选择系统：支持单选、Ctrl+左键（加选/减选）、Shift+左键（范围多选）。
// 3. 拖拽父子级级联 (Drag & Drop)：支持将一个实体拖拽到另一个实体上使其成为子节点，并自动防循环嵌套，支持插入排序。
// 4. 右键上下文菜单 (Context Menu)：创建空实体、3D物体(Cube/Sphere/Plane)、天空盒、灯光、相机、后处理体积、复制、删除、解除父子级。
// 5. 键盘快捷键响应：支持 Delete 键删除，Ctrl+D 键复制选中的实体。
// 6. 眼睛图标 (Visibility Toggle)：支持在节点右侧快速启用/禁用实体的显示状态及其层级灰显。
// 7. 内容浏览器拖入：支持从 ContentBrowser 拖入模型文件直接实例化到场景。
// 8. 聚合属性面板 (Properties Panel)：管理并向 PropertiesPanel 分发当前选中的实体列表。
// =========================================================================

#pragma once

#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "UI_Widgets.hpp"
#include "PropertiesPanel.hpp"
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

        Entity GetSelectedEntity() const { return m_SelectedEntities.empty() ? Entity{} : m_SelectedEntities.back(); }
        const std::vector<Entity>& GetSelectedEntities() const { return m_SelectedEntities; }

        void SetSelectedEntity(Entity entity) {
            m_SelectedEntities.clear();
            if (entity) m_SelectedEntities.push_back(entity);
            m_SelectionDirty = true;
        }

        void ToggleEntitySelection(Entity entity) {
            auto it = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity);
            if (it != m_SelectedEntities.end()) {
                m_SelectedEntities.erase(it);
            } else {
                m_SelectedEntities.push_back(entity);
            }
            m_SelectionDirty = true;
        }

        void ClearSelection() { m_SelectedEntities.clear(); m_SelectionDirty = true; }
        bool IsEntitySelected(Entity entity) const {
            return std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity) != m_SelectedEntities.end();
        }

        PropertiesPanel& GetPropertiesPanel() { return m_PropertiesPanel; }

    private:
        void DrawEntityNode(Entity entity);

    private:
        std::shared_ptr<Scene> m_Context;
        std::vector<Entity> m_SelectedEntities;

        std::vector<Entity> m_VisibleNodes;
        Entity m_LastClickedEntity = {};
        Entity m_ShiftClickTarget = {};

        std::vector<Entity> m_EntitiesToDestroy;
        std::vector<Entity> m_EntitiesToUnparent;
        std::vector<Entity> m_EntitiesToDuplicate;
        Entity m_PrefabEntity;  // right-click → Create Prefab

        std::vector<Entity> m_LastSentEntities; // 防重复同步
        bool m_SelectionDirty = false; // 强制同步标记：用户点击实体时置 true，解决选中同一实体不刷新的问题

        PropertiesPanel m_PropertiesPanel;
    };

}
