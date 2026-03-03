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
            // 1. 正常遍历根节点渲染树
            auto view = m_Context->Reg().view<TagComponent, RelationshipComponent>();
            for (auto entityID : view) {
                auto& rel = view.get<RelationshipComponent>(entityID);
                if (rel.Parent == entt::null) {
                    Entity entity{ entityID, m_Context.get() };
                    DrawEntityNode(entity);
                }
            }

            // ========================================================
            // 核心修复：处理空白区域的所有交互（左键取消、右键新建、拖拽解绑）
            // ========================================================
            
            // 计算剩余空白区域大小，并创建一个填满它的隐形按钮
            ImVec2 remainSize = ImGui::GetContentRegionAvail();
            if (remainSize.y < 50.0f) remainSize.y = 50.0f; 
            ImGui::InvisibleButton("##HierarchyEmptyArea", remainSize);

            // [交互 1]：左键点击这个隐形按钮 -> 取消选中
            if (ImGui::IsItemClicked(0)) {
                m_SelectionContext = {};
            }

            // [交互 2]：右键点击这个隐形按钮 -> 弹出新建菜单！
            if (ImGui::BeginPopupContextItem("HierarchySpacePopup")) {
                if (ImGui::MenuItem("Create Empty Entity")) {
                    m_Context->CreateEntity("Empty Entity");
                }
                ImGui::EndPopup();
            }

            // [交互 3]：将实体拖放到隐形按钮上 -> 解除父子关系 (回到根目录)
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_PAYLOAD")) {
                    entt::entity droppedID = *(entt::entity*)payload->Data;
                    m_EntityToUnparent = { droppedID, m_Context.get() }; 
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();
        
        ImGui::Begin("Properties");
        if (m_SelectionContext) DrawComponents(m_SelectionContext);
        ImGui::End();

        // 处理删除
        if (m_EntityToDestroy) {
            if (m_SelectionContext == m_EntityToDestroy) m_SelectionContext = {};
            m_Context->DestroyEntity(m_EntityToDestroy);
            m_EntityToDestroy = {};
        }

        // 处理解绑 (Unparent)
        if (m_EntityToUnparent) {
            m_EntityToUnparent.SetParent({}); 
            m_EntityToUnparent = {};
        }
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
        auto& tag = entity.GetComponent<TagComponent>().Tag;
        
        std::string icon = ICON_FA_CUBE; 
        if (entity.HasComponent<CameraComponent>()) icon = ICON_FA_VIDEO; 
        else if (entity.HasComponent<SpriteRendererComponent>()) icon = ICON_FA_PAINT_BRUSH; 
        
        std::string displayString = icon + " " + tag;

        ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) 
                                 | ImGuiTreeNodeFlags_OpenOnArrow 
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
        
        auto& rel = entity.GetComponent<RelationshipComponent>();
        if (rel.Children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;

        bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "%s", displayString.c_str());

        if (ImGui::IsItemClicked()) m_SelectionContext = entity;

        // 拖放源
        if (ImGui::BeginDragDropSource()) {
            entt::entity entityID = entity;
            ImGui::SetDragDropPayload("ENTITY_PAYLOAD", &entityID, sizeof(entt::entity));
            ImGui::Text("%s", tag.c_str());
            ImGui::EndDragDropSource();
        }

        // 拖放目标 (放置到别的物体上)
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_PAYLOAD")) {
                entt::entity droppedID = *(entt::entity*)payload->Data;
                Entity droppedEntity{ droppedID, m_Context.get() };
                droppedEntity.SetParent(entity);
            }
            ImGui::EndDragDropTarget();
        }

        // 右键菜单
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete Entity")) {
                m_EntityToDestroy = entity; // 标记延迟删除
            }
            if (ImGui::MenuItem("Unparent (Move to Root)")) {
                m_EntityToUnparent = entity; // 标记延迟解绑
            }
            ImGui::EndPopup();
        }

        if (opened) {
            // 注意：这里需要再次检查 entity 是否被删，防止在该帧后续递归中崩溃
            if (m_EntityToDestroy != entity) {
                // 必须拷贝一份 Children，防止在循环中由于 SetParent 改变 Children 导致迭代器失效
                std::vector<entt::entity> children = rel.Children;
                for (auto childID : children) {
                    DrawEntityNode({ childID, m_Context.get() });
                }
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