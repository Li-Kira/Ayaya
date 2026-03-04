#pragma once

#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include <memory>

namespace Ayaya {

    class SceneHierarchyPanel {
    public:
        SceneHierarchyPanel() = default;
        SceneHierarchyPanel(const std::shared_ptr<Scene>& context);

        void SetContext(const std::shared_ptr<Scene>& context);
        void OnImGuiRender();

        // ==========================================
        // 新增：向外暴露当前选中的实体
        // ==========================================
        Entity GetSelectedEntity() const { return m_SelectionContext; }

        // ==========================================
        // 新增：允许外部（如视口鼠标点击）设置当前选中物体
        // ==========================================
        void SetSelectedEntity(Entity entity) { m_SelectionContext = entity; }

    private:
        void DrawEntityNode(Entity entity);
        void DrawComponents(Entity entity);

    private:
        std::shared_ptr<Scene> m_Context;
        Entity m_SelectionContext; // 当前在大纲中被选中的实体

        Entity m_EntityToDestroy = {};  // 延迟删除标记
        Entity m_EntityToUnparent = {}; // 新增：延迟解绑标记
        Entity m_EntityToDuplicate = {};// 新增：延迟复制标记
    };

}