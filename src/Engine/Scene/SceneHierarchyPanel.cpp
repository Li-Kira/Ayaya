#include "ayapch.h"
#include "SceneHierarchyPanel.hpp"
#include "Engine/Scene/Components.hpp"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace Ayaya {

    SceneHierarchyPanel::SceneHierarchyPanel(const std::shared_ptr<Scene>& context) {
        SetContext(context);
    }

    void SceneHierarchyPanel::SetContext(const std::shared_ptr<Scene>& context) {
        m_Context = context;
        m_SelectionContext = {}; // 切换场景时清空选中状态
    }

    void SceneHierarchyPanel::OnImGuiRender() {
        // ==========================================
        // 1. 渲染 Scene Hierarchy (场景大纲)
        // ==========================================
        ImGui::Begin("Scene Hierarchy");

        if (m_Context) {
            // 遍历注册表中的所有实体
            auto view = m_Context->Reg().view<TagComponent>();
            for (auto entityID : view) {
                Entity entity{ entityID, m_Context.get() };
                DrawEntityNode(entity);
            }

            // 如果点击了面板的空白处，取消选中
            if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
                m_SelectionContext = {};
            }
        }
        ImGui::End();

        // ==========================================
        // 2. 渲染 Properties (属性面板)
        // ==========================================
        ImGui::Begin("Properties");

        if (m_SelectionContext) {
            DrawComponents(m_SelectionContext);
        }

        ImGui::End();
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
        auto& tag = entity.GetComponent<TagComponent>().Tag;

        // 设置树节点的样式：如果当前实体是被选中的，则高亮显示
        ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        
        // 强制把 Entity 强转为 void* 作为 ImGui 的唯一 ID
        bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "%s", tag.c_str());

        if (ImGui::IsItemClicked()) {
            m_SelectionContext = entity;
        }

        // 我们目前没有父子层级关系，所以如果它被展开了，直接 Pop 掉即可
        if (opened) {
            ImGui::TreePop();
        }
    }

    void SceneHierarchyPanel::DrawComponents(Entity entity) {
        // --- 绘制 Tag 组件 ---
        if (entity.HasComponent<TagComponent>()) {
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            
            // ImGui InputText 需要一个 char 数组
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, tag.c_str(), sizeof(buffer) - 1);
            
            if (ImGui::InputText("Tag", buffer, sizeof(buffer))) {
                tag = std::string(buffer); // 回写修改后的名字
            }
        }

        ImGui::Separator();

        // --- 绘制 Transform 组件 ---
        if (entity.HasComponent<TransformComponent>()) {
            // 默认展开 Transform 面板
            if (ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Transform")) {
                auto& transform = entity.GetComponent<TransformComponent>();

                ImGui::DragFloat3("Position", glm::value_ptr(transform.Translation), 0.1f);
                
                // 内部使用弧度制，但 UI 上我们通常显示角度制，方便人类阅读
                glm::vec3 rotation = glm::degrees(transform.Rotation);
                if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotation), 1.0f)) {
                    transform.Rotation = glm::radians(rotation); // 回写时转回弧度
                }

                ImGui::DragFloat3("Scale", glm::value_ptr(transform.Scale), 0.1f);

                ImGui::TreePop();
            }
        }
    }

}