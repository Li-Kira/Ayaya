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
            // ========================================================
            // 修复 1：必须使用我们自己维护的有序 RootEntities 列表！
            // 注意：必须拷贝一份 (auto rootEntities = ...)，因为拖拽过程中会修改原数组，
            // 如果直接引用遍历，会导致 C++ 迭代器失效崩溃。
            // ========================================================
            auto rootEntities = m_Context->GetRootEntities();
            for (auto entityID : rootEntities) {
                Entity entity{ entityID, m_Context.get() };
                DrawEntityNode(entity);
            }

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

        // 处理复制
        if (m_EntityToDuplicate) {
            Entity newEntity = m_Context->DuplicateEntity(m_EntityToDuplicate);
            // 复制完成后，自动选中新生成的物体，提升体验
            m_SelectionContext = newEntity; 
            m_EntityToDuplicate = {};
        }

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

        // =======================================================
        // 核心交互：带有拖拽重排 (Reorder) 指示线的 DropTarget 逻辑
        // =======================================================
        if (ImGui::BeginDragDropTarget()) {
            // 使用 AcceptBeforeDelivery 让我们在鼠标松开前就能计算反馈并画线
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_PAYLOAD", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                
                float mouseY = ImGui::GetMousePos().y;
                float itemMinY = ImGui::GetItemRectMin().y;
                float itemMaxY = ImGui::GetItemRectMax().y;
                float itemHeight = itemMaxY - itemMinY;

                // 判断拖拽落点属于上侧、下侧还是中间
                bool insertBefore = mouseY < itemMinY + itemHeight * 0.25f;
                bool insertAfter  = mouseY > itemMaxY - itemHeight * 0.25f;
                bool reparent     = !insertBefore && !insertAfter;

                // 画出黄色的提示线
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImVec2 minRect = ImGui::GetItemRectMin();
                ImVec2 maxRect = ImGui::GetItemRectMax();
                
                if (insertBefore) {
                    drawList->AddLine(ImVec2(minRect.x, itemMinY), ImVec2(maxRect.x, itemMinY), IM_COL32(255, 215, 0, 255), 2.0f);
                } else if (insertAfter) {
                    drawList->AddLine(ImVec2(minRect.x, itemMaxY), ImVec2(maxRect.x, itemMaxY), IM_COL32(255, 215, 0, 255), 2.0f);
                } else {
                    drawList->AddRect(minRect, maxRect, IM_COL32(255, 215, 0, 255), 0.0f, 0, 2.0f); // 画边框表示成为子节点
                }

                // 鼠标真正松开时 (IsDelivery) 触发换爹与排序
                if (payload->IsDelivery()) {
                    entt::entity droppedID = *(entt::entity*)payload->Data;
                    Entity droppedEntity{ droppedID, m_Context.get() };

                    if (insertBefore) {
                        Entity parent{ entity.GetComponent<RelationshipComponent>().Parent, m_Context.get() };
                        droppedEntity.SetParent(parent);
                        droppedEntity.MoveTo(entity, true);  // 移到前面
                    } else if (insertAfter) {
                        Entity parent{ entity.GetComponent<RelationshipComponent>().Parent, m_Context.get() };
                        droppedEntity.SetParent(parent);
                        droppedEntity.MoveTo(entity, false); // 移到后面
                    } else {
                        droppedEntity.SetParent(entity);     // 变成子节点
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        // 修复 2：删除了下方多余的旧 BeginDragDropTarget() 块，防止冲突覆盖。

        // 右键菜单
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Duplicate Entity")) {
                m_EntityToDuplicate = entity; // 标记延迟复制
            }
            if (ImGui::MenuItem("Delete Entity")) {
                m_EntityToDestroy = entity;
            }
            if (ImGui::MenuItem("Unparent")) {
                m_EntityToUnparent = entity; 
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
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, tag.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Tag", buffer, sizeof(buffer))) tag = std::string(buffer);
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
                
                // 【核心修改】：监听 Checkbox 的变化，保证全局只有一个 Primary Camera
                if (ImGui::Checkbox("Primary Camera", &cameraComp.Primary)) {
                    if (cameraComp.Primary) {
                        // 如果勾选了这个相机，把场景里其他所有相机的 Primary 都设为 false
                        auto view = m_Context->Reg().view<CameraComponent>();
                        for (auto entityID : view) {
                            if (entityID != (entt::entity)entity) {
                                view.get<CameraComponent>(entityID).Primary = false;
                            }
                        }
                    }
                }
                
                ImGui::Checkbox("Fixed Aspect Ratio", &cameraComp.FixedAspectRatio);
                ImGui::TreePop();
            }
        }
    }

}