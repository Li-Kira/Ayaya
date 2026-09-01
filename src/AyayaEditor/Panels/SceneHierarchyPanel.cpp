#include "ayapch.h"
#include "SceneHierarchyPanel.hpp"
#include "../EditorLayer.hpp"
#include "Engine/Scene/Components.hpp"
#include "Asset/AssetManager.hpp"
#include "Asset/Prefab.hpp"
#include "Renderer/Model.hpp"
#include "Utils/PlatformUtils.hpp"

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
        // Clear all pending entity operation lists — their Entity objects hold
        // raw m_Scene pointers to the OLD scene. After a scene switch the old
        // scene may be destroyed; operating on stale entities would be use-after-free.
        m_EntitiesToDestroy.clear();
        m_EntitiesToUnparent.clear();
        m_EntitiesToDuplicate.clear();
        m_PrefabEntity = {};
        m_LastClickedEntity = {};
        m_ShiftClickTarget = {};
        m_PropertiesPanel.SetContext(context);
    }

    void SceneHierarchyPanel::OnImGuiRender() {
        // 同步选中实体到属性面板：selection 变化 或 用户重新点击了已选中的实体
        if (m_SelectionDirty || m_LastSentEntities != m_SelectedEntities) {
            m_PropertiesPanel.SetSelectedEntities(m_SelectedEntities);
            m_LastSentEntities = m_SelectedEntities;
            m_SelectionDirty = false;
        }
        m_PropertiesPanel.OnImGuiRender();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Scene Hierarchy");
        float uiScale = ImGui::GetIO().FontGlobalScale;

        m_VisibleNodes.clear();

        if (m_Context) {
            // -- Match window background to table row colors so empty area blends in --
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg,  ImVec4(0.12f, 0.12f, 0.13f, 1.0f));

            // Subtle alternating row backgrounds
            ImGui::PushStyleColor(ImGuiCol_TableRowBg,    ImVec4(0.12f, 0.12f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.10f, 0.10f, 0.11f, 1.0f));

            // -- Engine-blue selection highlight --
            ImVec4 selBlue = ImVec4(0.20f, 0.38f, 0.82f, 0.70f);
            ImGui::PushStyleColor(ImGuiCol_Header,        selBlue);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.45f, 0.90f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.16f, 0.32f, 0.70f, 0.75f));

            static ImGuiTableFlags tableFlags =
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_NoBordersInBody |
                ImGuiTableFlags_NoSavedSettings;

            if (ImGui::BeginTable("##SceneHierarchyTable", 2, tableFlags))
            {
                ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##Vis",   ImGuiTableColumnFlags_WidthFixed,
                                        28.0f * uiScale);

                auto rootEntities = m_Context->GetRootEntities();
                for (auto entityID : rootEntities) {
                    Entity entity{ entityID, m_Context.get() };
                    DrawEntityNode(entity);
                }

                ImGui::EndTable();
            }

            ImGui::PopStyleColor(7);  // WindowBg, ChildBg, TableRowBg, TableRowBgAlt, Header x3

            // Empty area below the table (deselect + drop-to-root)
            ImVec2 remainSize = ImGui::GetContentRegionAvail();
            if (remainSize.y < 50.0f) remainSize.y = 50.0f;
            ImGui::InvisibleButton("##HierarchyEmptyArea", remainSize);

            if (ImGui::IsItemClicked(0)) {
                ClearSelection();
            }

            UI::PushPopupStyles(280.0f);
            if (ImGui::BeginPopupContextItem("HierarchySpacePopup")) {
                UI::DrawMenuHeader("Create Entity");

                if (UI::DrawNativeMenuItem("Create Empty Entity", ICON_FA_PLUS)) {
                    Entity newEntity = m_Context->CreateEntity("Empty Entity");
                    SetSelectedEntity(newEntity);
                }

                if (UI::BeginNativeMenu("3D Object", ICON_FA_CUBE)) {
                    if (UI::DrawNativeMenuItem("Cube",   ICON_FA_CUBE)) {
                        Entity entity = m_Context->CreateEntity("Cube");
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        mrc.ModelHandle = AssetManager::GetBuiltInCube();
                        mrc.MaterialHandle = AssetManager::GetBuiltInMaterial();
                        SetSelectedEntity(entity);
                    }
                    if (UI::DrawNativeMenuItem("Sphere", ICON_FA_CIRCLE)) {
                        Entity entity = m_Context->CreateEntity("Sphere");
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        mrc.ModelHandle = AssetManager::GetBuiltInSphere();
                        mrc.MaterialHandle = AssetManager::GetBuiltInMaterial();
                        SetSelectedEntity(entity);
                    }
                    if (UI::DrawNativeMenuItem("Plane",  ICON_FA_SQUARE)) {
                        Entity entity = m_Context->CreateEntity("Plane");
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        mrc.ModelHandle = AssetManager::GetBuiltInPlane();
                        mrc.MaterialHandle = AssetManager::GetBuiltInMaterial();
                        SetSelectedEntity(entity);
                    }
                    if (UI::DrawNativeMenuItem("Grid",   ICON_FA_TH)) {
                        Entity entity = m_Context->CreateEntity("Grid");
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        mrc.ModelHandle = AssetManager::GetBuiltInGrid();
                        mrc.MaterialHandle = AssetManager::GetBuiltInMaterial();
                        SetSelectedEntity(entity);
                    }
                    UI::EndNativeMenu();
                }

                if (UI::DrawNativeMenuItem("Create Skybox", ICON_FA_CLOUD_SUN)) {
                    Entity skyEntity = m_Context->CreateEntity("Skybox");
                    auto& envComp = skyEntity.AddComponent<EnvironmentComponent>();
                    envComp.Type = EnvironmentType::None;
                    envComp.Intensity = 30000.0f;
                    envComp.AmbientColor = { 0.0f, 0.0f, 0.0f };
                    m_SelectedEntities.clear();
                    m_SelectedEntities.push_back(skyEntity);
                }

                if (UI::BeginNativeMenu("Light", ICON_FA_LIGHTBULB)) {
                    if (UI::DrawNativeMenuItem("Directional Light", ICON_FA_SUN)) {
                        Entity entity = m_Context->CreateEntity("Directional Light");
                        auto& lightTransform = entity.GetComponent<TransformComponent>();
                        lightTransform.Rotation = glm::radians(glm::vec3(-45.0f, 45.0f, 0.0f));
                        entity.AddComponent<DirectionalLightComponent>();
                        SetSelectedEntity(entity);
                    }
                    if (UI::DrawNativeMenuItem("Point Light", ICON_FA_LIGHTBULB)) {
                        Entity entity = m_Context->CreateEntity("Point Light");
                        entity.AddComponent<PointLightComponent>();
                        entity.GetComponent<PointLightComponent>().LuminousPower = 1500.0f;
                        SetSelectedEntity(entity);
                    }
                    if (UI::DrawNativeMenuItem("Spot Light", ICON_FA_LIGHTBULB)) {
                        Entity entity = m_Context->CreateEntity("Spot Light");
                        entity.AddComponent<SpotLightComponent>();
                        entity.GetComponent<SpotLightComponent>().LuminousPower = 1500.0f;
                        SetSelectedEntity(entity);
                    }
                    UI::EndNativeMenu();
                }

                if (UI::DrawNativeMenuItem("Camera", ICON_FA_VIDEO)) {
                    Entity entity = m_Context->CreateEntity("Camera");
                    auto& cc = entity.AddComponent<CameraComponent>();
                    cc.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
                    cc.Primary = false;
                    SetSelectedEntity(entity);
                }

                if (UI::DrawNativeMenuItem("Post Process Volume", ICON_FA_MAGIC)) {
                    Entity ppvEntity = m_Context->CreateEntity("Post Process Volume");
                    ppvEntity.AddComponent<PostProcessVolumeComponent>();
                    SetSelectedEntity(ppvEntity);
                }

                if (UI::BeginNativeMenu("UI", ICON_FA_OBJECT_GROUP)) {
                    auto getOrCreateCanvas = [&]() -> Entity {
                        auto view = m_Context->Reg().view<CanvasComponent>();
                        if (view.begin() != view.end())
                            return { *view.begin(), m_Context.get() };
                        Entity canvas = m_Context->CreateEntity("Canvas");
                        auto& rt = canvas.AddComponent<RectTransformComponent>();
                        rt.AnchorMin = { 0.0f, 0.0f };
                        rt.AnchorMax = { 1.0f, 1.0f };
                        rt.Size = { 0.0f, 0.0f };
                        canvas.AddComponent<CanvasComponent>();
                        return canvas;
                    };

                    if (UI::DrawNativeMenuItem("Canvas",    ICON_FA_DESKTOP))       { Entity entity = getOrCreateCanvas(); SetSelectedEntity(entity); }
                    if (UI::DrawNativeMenuItem("UI Image",  ICON_FA_IMAGE))         { Entity entity = m_Context->CreateEntity("Image");  auto& rt = entity.AddComponent<RectTransformComponent>(); rt.Size = { 128.0f, 128.0f }; entity.AddComponent<UIImageComponent>();   entity.SetParent(getOrCreateCanvas()); SetSelectedEntity(entity); }
                    if (UI::DrawNativeMenuItem("UI Text",   ICON_FA_FONT))          { Entity entity = m_Context->CreateEntity("Text");   auto& rt = entity.AddComponent<RectTransformComponent>(); rt.Size = { 256.0f, 32.0f };  entity.AddComponent<UITextComponent>();   entity.SetParent(getOrCreateCanvas()); SetSelectedEntity(entity); }
                    if (UI::DrawNativeMenuItem("UI Button", ICON_FA_HAND_POINTER))  { Entity entity = m_Context->CreateEntity("Button"); auto& rt = entity.AddComponent<RectTransformComponent>(); rt.Size = { 160.0f, 48.0f };  entity.AddComponent<UIImageComponent>(); entity.AddComponent<UIButtonComponent>(); entity.SetParent(getOrCreateCanvas()); SetSelectedEntity(entity); }
                    UI::EndNativeMenu();
                }

                ImGui::EndPopup();
            }
            UI::PopPopupStyles();

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
                                UUID h = droppedHandle; auto ctx = m_Context; auto* self = this;
                                Application::SubmitDeferredAction([ctx, self, h]() {
                                    auto m = AssetManager::GetAsset<Model>(h);
                                    if (m) { Entity e = ctx->InstantiateModel(m); if (e) self->SetSelectedEntity(e); }
                                });
                            }
                        }
                        else if (meta.Type == AssetType::Prefab) {
                            auto prefab = AssetManager::GetAsset<Prefab>(droppedHandle);
                            if (prefab) {
                                UUID h = droppedHandle; auto ctx = m_Context; auto* self = this;
                                Application::SubmitDeferredAction([ctx, self, h]() {
                                    auto p = AssetManager::GetAsset<Prefab>(h);
                                    if (p) {
                                        Entity e = ctx->InstantiatePrefab(p.get());
                                        if (e) {
                                            self->SetSelectedEntity(e);
                                            // Invalidate GDR caches to force a clean SSBO rebuild.
                                            // Mirrors the pattern in EditorLayer::NewScene/OpenScene.
                                            auto& editor = EditorLayer::Get();
                                            editor.GetSceneRenderer()->ResetGDRCaches();
                                            editor.GetGameRenderer()->ResetGDRCaches();
                                        }
                                    }
                                });
                            }
                        }
                    }
                }

                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

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

        // Process Create Prefab
        if (m_PrefabEntity && m_Context) {
            std::string defaultName = m_PrefabEntity.GetComponent<TagComponent>().Tag;
            std::string filepath = FileDialogs::SaveFile(
                "Ayaya Prefab (*.prefab)\0*.prefab\0",
                defaultName + ".prefab");
            if (!filepath.empty()) {
                // Strip any existing extension (.ayaya, etc.) and force .prefab
                auto dot = filepath.rfind('.');
                auto slash = filepath.rfind('/');
#ifdef AYAYA_PLATFORM_WINDOWS
                auto bslash = filepath.rfind('\\');
                if (bslash != std::string::npos && bslash > slash) slash = bslash;
#endif
                if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                    filepath = filepath.substr(0, dot);
                filepath += ".prefab";
                auto prefab = std::make_shared<Prefab>();

                // Clone entity tree into prefab's mini scene
                Scene* pfScene = prefab->GetScene();
                std::function<Entity(Entity, Scene*)> cloneRecursive = [&](Entity src, Scene* srcScene) -> Entity {
                    Entity dst = pfScene->CreateEntity(src.GetComponent<TagComponent>().Tag);
                    dst.GetComponent<TransformComponent>() = src.GetComponent<TransformComponent>();
                    auto copy = [&](auto& dstEnt, auto& srcEnt) {
                        using T = std::decay_t<decltype(srcEnt)>;
                        if (srcEnt.template HasComponent<T>()) dstEnt.template AddComponent<T>(srcEnt.template GetComponent<T>());
                    };
                    // Copy common components
                    if (src.HasComponent<MeshRendererComponent>())
                        pfScene->Reg().emplace<MeshRendererComponent>(dst.GetEntityHandle(), src.GetComponent<MeshRendererComponent>());
                    if (src.HasComponent<CameraComponent>())
                        pfScene->Reg().emplace<CameraComponent>(dst.GetEntityHandle(), src.GetComponent<CameraComponent>());
                    if (src.HasComponent<SpriteRendererComponent>())
                        pfScene->Reg().emplace<SpriteRendererComponent>(dst.GetEntityHandle(), src.GetComponent<SpriteRendererComponent>());
                    if (src.HasComponent<DirectionalLightComponent>())
                        pfScene->Reg().emplace<DirectionalLightComponent>(dst.GetEntityHandle(), src.GetComponent<DirectionalLightComponent>());
                    if (src.HasComponent<PointLightComponent>())
                        pfScene->Reg().emplace<PointLightComponent>(dst.GetEntityHandle(), src.GetComponent<PointLightComponent>());
                    if (src.HasComponent<SpotLightComponent>())
                        pfScene->Reg().emplace<SpotLightComponent>(dst.GetEntityHandle(), src.GetComponent<SpotLightComponent>());

                    auto& srcRel = src.GetComponent<RelationshipComponent>();
                    for (auto childID : srcRel.Children) {
                        Entity srcChild{ childID, srcScene };
                        Entity dstChild = cloneRecursive(srcChild, srcScene);
                        dstChild.SetParent(dst, false);  // handles root-list removal, cycle guard, PropagateActiveState
                    }
                    return dst;
                };

                Entity root = cloneRecursive(m_PrefabEntity, m_Context.get());
                prefab->SetRootEntity(root);
                prefab->Save(filepath);

                // Register in AssetManager — writes .meta and adds to registry
                UUID prefabHandle = AssetManager::ImportAsset(filepath);
                // Force immediate load so the asset is available without waiting
                // for the FileWatcher poll cycle
                if (prefabHandle != 0) {
                    AssetManager::GetAsset<Prefab>(prefabHandle);
                }
            }
            m_PrefabEntity = {};
        }
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
        m_VisibleNodes.push_back(entity);

        auto& tagComp = entity.GetComponent<TagComponent>();
        auto& tag = tagComp.Tag;
        auto iconInfo = UI::GetEntityIconInfo(entity);
        bool isSelected = IsEntitySelected(entity);

        ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0)
            | ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_SpanAllColumns
            | ImGuiTreeNodeFlags_FramePadding;

        auto& rel = entity.GetComponent<RelationshipComponent>();
        if (rel.Children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;

        bool activeInHierarchy = entity.IsActiveInHierarchy();

        // -- Begin a new table row --
        ImGui::TableNextRow();

        // =========================================
        // Column 1: Eye visibility button
        // =========================================
        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
        if (!activeInHierarchy)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

        ImGui::PushID((uint32_t)entity);
        float btnW = 24.0f * ImGui::GetIO().FontGlobalScale;
        float btnH = ImGui::GetTextLineHeight();
        bool eyeClicked = ImGui::Button(
            tagComp.IsActive ? ICON_FA_EYE : ICON_FA_EYE_SLASH,
            ImVec2(btnW, btnH));
        if (eyeClicked) {
            tagComp.IsActive = !tagComp.IsActive;
            m_Context->PropagateActiveState(entity);
        }
        ImGui::PopID();

        if (!activeInHierarchy) ImGui::PopStyleColor(); // Text dim
        ImGui::PopStyleColor(3);  // Button, ButtonHovered, ButtonActive
        ImGui::PopStyleVar();     // FramePadding

        // =========================================
        // Column 0: Entity TreeNode
        // =========================================
        ImGui::TableSetColumnIndex(0);

        if (!activeInHierarchy)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

        // TreeNode with empty label — provides indent, arrow, hitbox & highlight.
        // Icon + text are rendered afterwards so text always starts at the same
        // X offset regardless of icon glyph width.
        bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "");

        // -- Click handling (guard against eye button clicks) --
        if (ImGui::IsItemClicked() && !eyeClicked) {
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

        // -- Drag-drop source (wraps TreeNode) --
        if (ImGui::BeginDragDropSource()) {
            entt::entity entityID = entity;
            ImGui::SetDragDropPayload("ENTITY_PAYLOAD", &entityID, sizeof(entt::entity));
            ImGui::Text("%s", tag.c_str());
            ImGui::EndDragDropSource();
        }

        // -- Drag-drop target (3-zone, uses TreeNode rect) --
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_PAYLOAD",
                    ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                float mouseY = ImGui::GetMousePos().y;
                float itemMinY = ImGui::GetItemRectMin().y;
                float itemMaxY = ImGui::GetItemRectMax().y;
                float itemHeight = itemMaxY - itemMinY;

                bool insertBefore = mouseY < itemMinY + itemHeight * 0.25f;
                bool insertAfter  = mouseY > itemMaxY - itemHeight * 0.25f;
                bool reparent     = !insertBefore && !insertAfter;

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImVec2 minRect = ImGui::GetItemRectMin();
                ImVec2 maxRect = ImGui::GetItemRectMax();

                if (insertBefore) {
                    drawList->AddLine(ImVec2(minRect.x, itemMinY), ImVec2(maxRect.x, itemMinY),
                                      IM_COL32(255, 215, 0, 255), 2.0f);
                } else if (insertAfter) {
                    drawList->AddLine(ImVec2(minRect.x, itemMaxY), ImVec2(maxRect.x, itemMaxY),
                                      IM_COL32(255, 215, 0, 255), 2.0f);
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

        // -- Context menu (bound to TreeNode, before icon+text overlay) --
        UI::PushPopupStyles();
        if (ImGui::BeginPopupContextItem()) {
            if (UI::DrawNativeMenuItem("Duplicate Entity", nullptr, "Ctrl+D")) {
                if (IsEntitySelected(entity)) m_EntitiesToDuplicate = m_SelectedEntities;
                else m_EntitiesToDuplicate.push_back(entity);
            }
            if (UI::DrawNativeMenuItem("Delete Entity", nullptr, "Del")) {
                if (IsEntitySelected(entity)) m_EntitiesToDestroy = m_SelectedEntities;
                else m_EntitiesToDestroy.push_back(entity);
            }
            if (UI::DrawNativeMenuItem("Unparent")) {
                if (IsEntitySelected(entity)) m_EntitiesToUnparent = m_SelectedEntities;
                else m_EntitiesToUnparent.push_back(entity);
            }
            UI::MenuSeparator();
            if (UI::DrawNativeMenuItem("Create Prefab")) {
                Entity target = IsEntitySelected(entity) ? *m_SelectedEntities.begin() : entity;
                m_PrefabEntity = target;
            }
            ImGui::EndPopup();
        }
        UI::PopPopupStyles();

        // --- Icon + text rendered separately for horizontal alignment ---
        ImGui::SameLine(0, 0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(iconInfo.Icon);

        // Fixed-width icon column: all entity names start at the same X
        const float kIconWidth = 28.0f * ImGui::GetIO().FontGlobalScale;
        float iconStartX = ImGui::GetItemRectMin().x;
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(iconStartX + kIconWidth,
                                         ImGui::GetCursorScreenPos().y));
        ImGui::TextUnformatted(tag.c_str());

        if (!activeInHierarchy)
            ImGui::PopStyleColor(); // Text dim

        // -- Render children recursively --
        if (opened) {
            bool isBeingDestroyed = std::find(m_EntitiesToDestroy.begin(), m_EntitiesToDestroy.end(), entity)
                                    != m_EntitiesToDestroy.end();
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
