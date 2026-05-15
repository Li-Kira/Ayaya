#include "ayapch.h"
#include "SceneHierarchyPanel.hpp"
#include "../EditorLayer.hpp"
#include "Engine/Scene/Components.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Model.hpp"

#include <imgui.h>
#include <cstring>
#include <IconsFontAwesome5.h>

namespace Ayaya {

    SceneHierarchyPanel::SceneHierarchyPanel(const std::shared_ptr<Scene>& context) {
        SetContext(context);
    }

    void SceneHierarchyPanel::SetContext(const std::shared_ptr<Scene>& context) {
        m_Context = context;
        m_SelectedEntities.clear();
        m_VisibleNodes.clear();
        m_LastSentEntities.clear();
        m_PropertiesPanel.SetContext(context);
    }

    void SceneHierarchyPanel::OnImGuiRender() {
        // 仅在选中实体变化时同步到属性面板（避免每帧覆盖资产检查模式）
        if (m_LastSentEntities != m_SelectedEntities) {
            m_PropertiesPanel.SetSelectedEntities(m_SelectedEntities);
            m_LastSentEntities = m_SelectedEntities;
        }
        m_PropertiesPanel.OnImGuiRender();

        ImGui::Begin("Scene Hierarchy");
        float uiScale = ImGui::GetIO().FontGlobalScale;

        m_VisibleNodes.clear();

        if (m_Context) {
            auto rootEntities = m_Context->GetRootEntities();
            for (auto entityID : rootEntities) {
                Entity entity{ entityID, m_Context.get() };
                DrawEntityNode(entity);
            }

            ImVec2 remainSize = ImGui::GetContentRegionAvail();
            if (remainSize.y < 50.0f) remainSize.y = 50.0f;
            ImGui::InvisibleButton("##HierarchyEmptyArea", remainSize);

            if (ImGui::IsItemClicked(0)) {
                ClearSelection();
            }

            if (ImGui::BeginPopupContextItem("HierarchySpacePopup")) {
                if (ImGui::MenuItem("Create Empty Entity")) {
                    Entity newEntity = m_Context->CreateEntity("Empty Entity");
                    SetSelectedEntity(newEntity);
                }

                if (ImGui::BeginMenu("3D Object")) {
                    if (ImGui::MenuItem("Cube")) {
                        Entity entity = m_Context->CreateEntity("Cube");
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        mrc.ModelHandle = AssetManager::GetBuiltInCube();
                        mrc.MaterialHandle = AssetManager::GetBuiltInMaterial();
                        SetSelectedEntity(entity);
                    }
                    if (ImGui::MenuItem("Sphere")) {
                        Entity entity = m_Context->CreateEntity("Sphere");
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        mrc.ModelHandle = AssetManager::GetBuiltInSphere();
                        mrc.MaterialHandle = AssetManager::GetBuiltInMaterial();
                        SetSelectedEntity(entity);
                    }
                    if (ImGui::MenuItem("Plane")) {
                        Entity entity = m_Context->CreateEntity("Plane");
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        mrc.ModelHandle = AssetManager::GetBuiltInPlane();
                        mrc.MaterialHandle = AssetManager::GetBuiltInMaterial();
                        SetSelectedEntity(entity);
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Create Skybox")) {
                    Entity skyEntity = m_Context->CreateEntity("Skybox");
                    auto& envComp = skyEntity.AddComponent<EnvironmentComponent>();
                    envComp.Type = EnvironmentType::None;
                    envComp.Intensity = 30000.0f;
                    envComp.AmbientColor = { 0.0f, 0.0f, 0.0f };
                    m_SelectedEntities.clear();
                    m_SelectedEntities.push_back(skyEntity);
                }

                if (ImGui::BeginMenu("Light")) {
                    if (ImGui::MenuItem("Directional Light")) {
                        Entity entity = m_Context->CreateEntity("Directional Light");
                        auto& lightTransform = entity.GetComponent<TransformComponent>();
                        lightTransform.Rotation = glm::radians(glm::vec3(-45.0f, 45.0f, 0.0f));
                        entity.AddComponent<DirectionalLightComponent>();
                        SetSelectedEntity(entity);
                    }
                    if (ImGui::MenuItem("Point Light")) {
                        Entity entity = m_Context->CreateEntity("Point Light");
                        entity.AddComponent<PointLightComponent>();
                        entity.GetComponent<PointLightComponent>().LuminousPower = 1500.0f;
                        SetSelectedEntity(entity);
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Camera")) {
                    Entity entity = m_Context->CreateEntity("Camera");
                    auto& cc = entity.AddComponent<CameraComponent>();
                    cc.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
                    cc.Primary = false;
                    SetSelectedEntity(entity);
                }

                if (ImGui::MenuItem("Post Process Volume")) {
                    Entity ppvEntity = m_Context->CreateEntity("Post Process Volume");
                    ppvEntity.AddComponent<PostProcessVolumeComponent>();
                    SetSelectedEntity(ppvEntity);
                }

                ImGui::EndPopup();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_PAYLOAD")) {
                    entt::entity droppedID = *(entt::entity*)payload->Data;
                    m_EntitiesToUnparent.push_back({ droppedID, m_Context.get() });
                }

                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    UUID droppedHandle = *(const UUID*)payload->Data;
                    if (droppedHandle != 0) {
                        AssetMetadata meta = AssetManager::GetMetadata(droppedHandle);
                        if (meta.Type == AssetType::Model) {
                            auto loadedModel = AssetManager::GetAsset<Model>(droppedHandle);
                            if (loadedModel) {
                                AYAYA_CORE_INFO("Instantiating Model to Scene via UUID: {0}", (uint64_t)droppedHandle);
                                Entity rootEntity = m_Context->InstantiateModel(loadedModel);
                                if (rootEntity) SetSelectedEntity(rootEntity);
                            }
                        }
                    }
                }

                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();

        // Shift 范围多选
        if (m_ShiftClickTarget) {
            if (m_LastClickedEntity) {
                auto itStart = std::find(m_VisibleNodes.begin(), m_VisibleNodes.end(), m_LastClickedEntity);
                auto itEnd = std::find(m_VisibleNodes.begin(), m_VisibleNodes.end(), m_ShiftClickTarget);

                if (itStart != m_VisibleNodes.end() && itEnd != m_VisibleNodes.end()) {
                    m_SelectedEntities.clear();
                    auto minIt = std::min(itStart, itEnd);
                    auto maxIt = std::max(itStart, itEnd);
                    for (auto it = minIt; it <= maxIt; ++it) m_SelectedEntities.push_back(*it);
                }
            } else {
                SetSelectedEntity(m_ShiftClickTarget);
                m_LastClickedEntity = m_ShiftClickTarget;
            }
            m_ShiftClickTarget = {};
        }

        // 快捷键处理
        if (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
                for (auto e : m_SelectedEntities) m_EntitiesToDestroy.push_back(e);
            }
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_D) && !ImGui::GetIO().WantTextInput) {
                for (auto e : m_SelectedEntities) m_EntitiesToDuplicate.push_back(e);
            }
        }

        // 批量处理复制
        if (!m_EntitiesToDuplicate.empty()) {
            std::vector<Entity> newSelections;
            for (auto entity : m_EntitiesToDuplicate) {
                Entity newEntity = m_Context->DuplicateEntity(entity);
                newSelections.push_back(newEntity);
            }
            m_SelectedEntities = newSelections;
            m_EntitiesToDuplicate.clear();
        }

        // 批量处理删除
        if (!m_EntitiesToDestroy.empty()) {
            for (auto entity : m_EntitiesToDestroy) {
                auto it = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity);
                if (it != m_SelectedEntities.end()) m_SelectedEntities.erase(it);
                m_Context->DestroyEntity(entity);
            }
            m_EntitiesToDestroy.clear();
        }

        // 批量处理解绑
        if (!m_EntitiesToUnparent.empty()) {
            for (auto entity : m_EntitiesToUnparent) {
                entity.SetParent({});
            }
            m_EntitiesToUnparent.clear();
        }
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
        m_VisibleNodes.push_back(entity);

        auto& tagComp = entity.GetComponent<TagComponent>();
        auto& tag = tagComp.Tag;

        std::string icon = ICON_FA_CUBE;
        if (entity.HasComponent<CameraComponent>()) icon = ICON_FA_VIDEO;
        else if (entity.HasComponent<SpriteRendererComponent>()) icon = ICON_FA_PAINT_BRUSH;
        else if (entity.HasComponent<MeshRendererComponent>()) icon = ICON_FA_PAINT_BRUSH;
        else if (entity.HasComponent<DirectionalLightComponent>()) icon = ICON_FA_SUN;
        else if (entity.HasComponent<PointLightComponent>()) icon = ICON_FA_LIGHTBULB;
        else if (entity.HasComponent<EnvironmentComponent>()) icon = ICON_FA_CLOUD_SUN;
        else if (entity.HasComponent<PostProcessVolumeComponent>()) icon = ICON_FA_MAGIC;

        std::string displayString = icon + " " + tag;

        bool isSelected = IsEntitySelected(entity);

        ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0)
            | ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_SpanAvailWidth;

        auto& rel = entity.GetComponent<RelationshipComponent>();
        if (rel.Children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;

        bool activeInHierarchy = entity.IsActiveInHierarchy();
        if (!activeInHierarchy) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }

        float cursorY = ImGui::GetCursorPosY();
        ImGui::SetNextItemAllowOverlap();

        bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "%s", displayString.c_str());

        if (ImGui::IsItemClicked()) {
            if (ImGui::GetIO().KeyShift) {
                m_ShiftClickTarget = entity;
            } else if (ImGui::GetIO().KeyCtrl) {
                ToggleEntitySelection(entity);
                m_LastClickedEntity = entity;
            } else {
                SetSelectedEntity(entity);
                m_LastClickedEntity = entity;
            }
        }

        if (ImGui::BeginDragDropSource()) {
            entt::entity entityID = entity;
            ImGui::SetDragDropPayload("ENTITY_PAYLOAD", &entityID, sizeof(entt::entity));
            ImGui::Text("%s", tag.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_PAYLOAD", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                float mouseY = ImGui::GetMousePos().y;
                float itemMinY = ImGui::GetItemRectMin().y;
                float itemMaxY = ImGui::GetItemRectMax().y;
                float itemHeight = itemMaxY - itemMinY;

                bool insertBefore = mouseY < itemMinY + itemHeight * 0.25f;
                bool insertAfter = mouseY > itemMaxY - itemHeight * 0.25f;
                bool reparent = !insertBefore && !insertAfter;

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImVec2 minRect = ImGui::GetItemRectMin();
                ImVec2 maxRect = ImGui::GetItemRectMax();

                if (insertBefore) {
                    drawList->AddLine(ImVec2(minRect.x, itemMinY), ImVec2(maxRect.x, itemMinY), IM_COL32(255, 215, 0, 255), 2.0f);
                } else if (insertAfter) {
                    drawList->AddLine(ImVec2(minRect.x, itemMaxY), ImVec2(maxRect.x, itemMaxY), IM_COL32(255, 215, 0, 255), 2.0f);
                } else {
                    drawList->AddRect(minRect, maxRect, IM_COL32(255, 215, 0, 255), 0.0f, 0, 2.0f);
                }

                if (payload->IsDelivery()) {
                    entt::entity droppedID = *(entt::entity*)payload->Data;
                    Entity droppedEntity{ droppedID, m_Context.get() };

                    if (insertBefore) {
                        Entity parent{ entity.GetComponent<RelationshipComponent>().Parent, m_Context.get() };
                        droppedEntity.SetParent(parent);
                        droppedEntity.MoveTo(entity, true);
                    } else if (insertAfter) {
                        Entity parent{ entity.GetComponent<RelationshipComponent>().Parent, m_Context.get() };
                        droppedEntity.SetParent(parent);
                        droppedEntity.MoveTo(entity, false);
                    } else {
                        droppedEntity.SetParent(entity);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Duplicate Entity")) {
                if (IsEntitySelected(entity)) m_EntitiesToDuplicate = m_SelectedEntities;
                else m_EntitiesToDuplicate.push_back(entity);
            }
            if (ImGui::MenuItem("Delete Entity")) {
                if (IsEntitySelected(entity)) m_EntitiesToDestroy = m_SelectedEntities;
                else m_EntitiesToDestroy.push_back(entity);
            }
            if (ImGui::MenuItem("Unparent")) {
                if (IsEntitySelected(entity)) m_EntitiesToUnparent = m_SelectedEntities;
                else m_EntitiesToUnparent.push_back(entity);
            }
            ImGui::EndPopup();
        }

        if (!activeInHierarchy) {
            ImGui::PopStyleColor();
        }

        float uiScaleBtn = ImGui::GetIO().FontGlobalScale;

        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f * uiScaleBtn);
        ImGui::SetCursorPosY(cursorY);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));

        std::string eyeIcon = tagComp.IsActive ? ICON_FA_EYE : ICON_FA_EYE_SLASH;

        if (!activeInHierarchy) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }

        ImGui::PushID((uint32_t)entity);
        if (ImGui::Button(eyeIcon.c_str(), ImVec2(24.0f * uiScaleBtn, ImGui::GetTextLineHeight()))) {
            tagComp.IsActive = !tagComp.IsActive;
        }
        ImGui::PopID();

        if (!activeInHierarchy) {
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        if (opened) {
            bool isBeingDestroyed = std::find(m_EntitiesToDestroy.begin(), m_EntitiesToDestroy.end(), entity) != m_EntitiesToDestroy.end();
            if (!isBeingDestroyed) {
                std::vector<entt::entity> children = rel.Children;
                for (auto childID : children) {
                    DrawEntityNode({ childID, m_Context.get() });
                }
            }
            ImGui::TreePop();
        }
    }
}
