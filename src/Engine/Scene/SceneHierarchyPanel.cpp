#include "ayapch.h"
#include "SceneHierarchyPanel.hpp"
#include "Engine/Scene/Components.hpp"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

// --- 引入 FontAwesome 图标宏 ---
#include <IconsFontAwesome5.h> 

namespace Ayaya {

    SceneHierarchyPanel::SceneHierarchyPanel(const std::shared_ptr<Scene>& context) {
        SetContext(context);
    }

    void SceneHierarchyPanel::SetContext(const std::shared_ptr<Scene>& context) {
        m_Context = context;
        m_SelectionContext = {}; // 切换场景时清空选中状态
    }

    void SceneHierarchyPanel::OnImGuiRender() {
        ImGui::Begin("Scene Hierarchy");

        if (m_Context) {
            // 核心修改：只遍历根节点（Parent 为 null 的实体）
            auto view = m_Context->Reg().view<TagComponent, RelationshipComponent>();
            for (auto entityID : view) {
                auto& rel = view.get<RelationshipComponent>(entityID);
                
                if (rel.Parent == entt::null) { // 如果是根节点，才从这里开始绘制
                    Entity entity{ entityID, m_Context.get() };
                    DrawEntityNode(entity);
                }
            }

            if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
                m_SelectionContext = {};
            }
        }
        ImGui::End();

        ImGui::Begin("Properties");
        if (m_SelectionContext) DrawComponents(m_SelectionContext);
        ImGui::End();
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
        auto& tag = entity.GetComponent<TagComponent>().Tag;
        
        // 动态图标逻辑
        std::string icon = ICON_FA_CUBE; 
        if (entity.HasComponent<CameraComponent>()) icon = ICON_FA_VIDEO; 
        else if (entity.HasComponent<SpriteRendererComponent>()) icon = ICON_FA_PAINT_BRUSH; 
        std::string displayString = icon + " " + tag;

        ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) 
                                 | ImGuiTreeNodeFlags_OpenOnArrow 
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
        
        // 核心修改：通过 RelationshipComponent 判断是否有子节点
        auto& rel = entity.GetComponent<RelationshipComponent>();
        bool hasChildren = !rel.Children.empty(); 
        
        if (!hasChildren) {
            flags |= ImGuiTreeNodeFlags_Leaf; // 如果没有子节点，隐藏箭头
        }

        bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "%s", displayString.c_str());

        if (ImGui::IsItemClicked()) {
            m_SelectionContext = entity;
        }

        if (opened) {
            // 核心修改：如果有子节点，递归绘制它们！
            for (auto childID : rel.Children) {
                Entity childEntity{ childID, m_Context.get() };
                DrawEntityNode(childEntity);
            }
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
            if (ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Transform")) {
                auto& transform = entity.GetComponent<TransformComponent>();

                ImGui::DragFloat3("Position", glm::value_ptr(transform.Translation), 0.1f);
                
                // UI 上显示角度制，方便人类阅读
                glm::vec3 rotation = glm::degrees(transform.Rotation);
                if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotation), 1.0f)) {
                    transform.Rotation = glm::radians(rotation); // 回写时转回弧度
                }

                ImGui::DragFloat3("Scale", glm::value_ptr(transform.Scale), 0.1f);

                ImGui::TreePop();
            }
        }

        // --- 绘制 Sprite Renderer 组件 ---
        if (entity.HasComponent<SpriteRendererComponent>()) {
            if (ImGui::TreeNodeEx((void*)typeid(SpriteRendererComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Sprite Renderer")) {
                auto& src = entity.GetComponent<SpriteRendererComponent>();
                ImGui::ColorEdit4("Color", glm::value_ptr(src.Color));
                ImGui::TreePop();
            }
        }

        // --- 绘制 Camera 组件 ---
        if (entity.HasComponent<CameraComponent>()) {
            if (ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Camera")) {
                auto& cameraComp = entity.GetComponent<CameraComponent>();
                ImGui::Checkbox("Primary Camera", &cameraComp.Primary);
                ImGui::Checkbox("Fixed Aspect Ratio", &cameraComp.FixedAspectRatio);
                ImGui::TreePop();
            }
        }
    }

}