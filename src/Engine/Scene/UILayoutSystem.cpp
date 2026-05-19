#include "ayapch.h"
#include "UILayoutSystem.hpp"
#include "Entity.hpp"
#include "Components.hpp"
#include "Core/Application.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    static void ProcessLayout(RectTransformComponent& rt, const glm::vec2& parentMin, const glm::vec2& parentMax, const glm::mat4& parentMatrix) {
        glm::vec2 parentSize = parentMax - parentMin;

        // 1. Anchor 拉伸尺寸
        glm::vec2 anchorSize = (rt.AnchorMax - rt.AnchorMin) * parentSize;
        rt.CalculatedSize = anchorSize + rt.Size;
        rt.CalculatedSize.x = std::max(rt.CalculatedSize.x, 0.0f);
        rt.CalculatedSize.y = std::max(rt.CalculatedSize.y, 0.0f);

        // 2. 局部 pivot 坐标
        glm::vec2 anchorRefPos = parentMin + rt.AnchorMin * parentSize + anchorSize * rt.Pivot;
        glm::vec2 localPos = anchorRefPos + rt.Position;

        // 3. 局部 TRS
        glm::mat4 localMat = glm::translate(glm::mat4(1.0f), glm::vec3(localPos, 0.0f));
        localMat = glm::rotate(localMat, glm::radians(rt.Rotation), glm::vec3(0.0f, 0.0f, 1.0f));
        localMat = glm::scale(localMat, glm::vec3(rt.Scale, 1.0f));

        // 4. 全局层级矩阵
        rt.HierarchyTransform = parentMatrix * localMat;

        // 5. 渲染矩阵 — 将 [0,1]² Quad 映射到像素
        glm::mat4 renderOffset = glm::translate(glm::mat4(1.0f),
            glm::vec3(-rt.Pivot * rt.CalculatedSize, 0.0f));
        renderOffset = glm::scale(renderOffset, glm::vec3(rt.CalculatedSize, 1.0f));
        rt.RenderTransform = rt.HierarchyTransform * renderOffset;

        // 6. AABB
        glm::vec3 wp = glm::vec3(rt.HierarchyTransform[3]);
        glm::vec2 wpos(wp.x, wp.y);
        rt.ScreenMin = wpos - rt.Pivot * rt.CalculatedSize * rt.Scale;
        rt.ScreenMax = rt.ScreenMin + rt.CalculatedSize * rt.Scale;
        rt.LayoutDirty = false;
    }

    static void RecurseLayout(Scene* scene, entt::entity entity, const glm::vec2& pMin, const glm::vec2& pMax, const glm::mat4& pMatrix) {
        Entity e{ entity, scene };
        if (!e.HasComponent<RectTransformComponent>()) return;
        auto& rt = e.GetComponent<RectTransformComponent>();

        ProcessLayout(rt, pMin, pMax, pMatrix);

        // 子节点在父节点 LOCAL 空间布局 (0, 0) → CalculatedSize
        if (e.HasComponent<RelationshipComponent>()) {
            for (auto child : e.GetComponent<RelationshipComponent>().Children)
                RecurseLayout(scene, child, {0.0f, 0.0f}, rt.CalculatedSize, rt.HierarchyTransform);
        }
    }

    void UILayoutSystem::Update(Scene& scene) {
        auto view = scene.Reg().view<CanvasComponent, RectTransformComponent>();
        for (auto entity : view) {
            float w = (float)Application::Get().GetWindow().GetWidth();
            float h = (float)Application::Get().GetWindow().GetHeight();
            RecurseLayout(&scene, entity, {0.0f, 0.0f}, {w, h}, glm::mat4(1.0f));
        }
    }

}
