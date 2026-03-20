#include "ayapch.h"
#include "SceneHierarchyPanel.hpp"
#include "Engine/Scene/Components.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/MaterialSerializer.hpp"

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
        ClearSelection(); // 切换场景时清空所有选中状态
    }

    void SceneHierarchyPanel::OnImGuiRender() {
        ImGui::Begin("Scene Hierarchy");

        // 每次渲染大纲前，清空可见节点顺序列表
        m_VisibleNodes.clear();

        if (m_Context) {
            auto rootEntities = m_Context->GetRootEntities();
            for (auto entityID : rootEntities) {
                Entity entity{ entityID, m_Context.get() };
                DrawEntityNode(entity);
            }

            // 计算剩余空白区域大小，并创建一个填满它的隐形按钮
            ImVec2 remainSize = ImGui::GetContentRegionAvail();
            if (remainSize.y < 50.0f) remainSize.y = 50.0f; 
            ImGui::InvisibleButton("##HierarchyEmptyArea", remainSize);

            // [交互 1]：左键点击这个隐形按钮 -> 取消全部选中
            if (ImGui::IsItemClicked(0)) {
                ClearSelection();
            }

            // [交互 2]：右键点击这个隐形按钮 -> 弹出新建菜单！
            if (ImGui::BeginPopupContextItem("HierarchySpacePopup")) {
                if (ImGui::MenuItem("Create Empty Entity")) {
                    Entity newEntity = m_Context->CreateEntity("Empty Entity");
                    SetSelectedEntity(newEntity); // 创建后自动选中
                }
                ImGui::EndPopup();
            }

            // [交互 3]：将实体拖放到隐形按钮上 -> 解除父子关系 (回到根目录)
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_PAYLOAD")) {
                    entt::entity droppedID = *(entt::entity*)payload->Data;
                    // 修改为 push_back 追加到数组中
                    m_EntitiesToUnparent.push_back({ droppedID, m_Context.get() }); 
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();

        // ==========================================
        //  [交互 4]：统一结算 Shift 范围多选逻辑
        // ==========================================
        if (m_ShiftClickTarget) {
            if (m_LastClickedEntity) {
                // 找到“锚点”和“目标”在当前可见列表中的索引
                auto itStart = std::find(m_VisibleNodes.begin(), m_VisibleNodes.end(), m_LastClickedEntity);
                auto itEnd = std::find(m_VisibleNodes.begin(), m_VisibleNodes.end(), m_ShiftClickTarget);
                
                if (itStart != m_VisibleNodes.end() && itEnd != m_VisibleNodes.end()) {
                    m_SelectedEntities.clear(); // Shift 单击通常会替换原有选择
                    
                    // 确保按顺序遍历（因为你可能是从下往上 Shift 点击）
                    auto minIt = std::min(itStart, itEnd);
                    auto maxIt = std::max(itStart, itEnd);
                    
                    for (auto it = minIt; it <= maxIt; ++it) {
                        m_SelectedEntities.push_back(*it);
                    }
                }
            } else {
                // 如果之前没点过任何东西，Shift 点击等同于普通单选
                SetSelectedEntity(m_ShiftClickTarget);
                m_LastClickedEntity = m_ShiftClickTarget;
            }
            m_ShiftClickTarget = {}; // 结算完毕，清空标记
        }
        
        // ==========================================
        // 属性面板调用
        // ==========================================
        ImGui::Begin("Properties");
        if (!m_SelectedEntities.empty()) {
            DrawComponents(); 
        }
        ImGui::End();

        // ==========================================
        // 批量处理复制
        // ==========================================
        if (!m_EntitiesToDuplicate.empty()) {
            std::vector<Entity> newSelections;
            for (auto entity : m_EntitiesToDuplicate) {
                Entity newEntity = m_Context->DuplicateEntity(entity);
                newSelections.push_back(newEntity);
            }
            m_SelectedEntities = newSelections; // 批量选中所有新生成的物体
            m_EntitiesToDuplicate.clear();
        }

        // ==========================================
        // 批量处理删除
        // ==========================================
        if (!m_EntitiesToDestroy.empty()) {
            for (auto entity : m_EntitiesToDestroy) {
                // 如果删除的物体在选中列表中，把它踢出去
                auto it = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity);
                if (it != m_SelectedEntities.end()) m_SelectedEntities.erase(it);
                
                m_Context->DestroyEntity(entity);
            }
            m_EntitiesToDestroy.clear();
        }

        // ==========================================
        // 批量处理解绑
        // ==========================================
        if (!m_EntitiesToUnparent.empty()) {
            for (auto entity : m_EntitiesToUnparent) {
                entity.SetParent({}); 
            }
            m_EntitiesToUnparent.clear();
        }
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity) {
        // --- 新增：只要进入了这个函数，说明该节点没有被折叠，记录进可见列表顺序中 ---
        m_VisibleNodes.push_back(entity);

        auto& tagComp = entity.GetComponent<TagComponent>();
        auto& tag = tagComp.Tag;
        
        std::string icon = ICON_FA_CUBE; 
        if (entity.HasComponent<CameraComponent>()) icon = ICON_FA_VIDEO; 
        else if (entity.HasComponent<SpriteRendererComponent>()) icon = ICON_FA_PAINT_BRUSH;
        else if (entity.HasComponent<MeshRendererComponent>()) icon = ICON_FA_PAINT_BRUSH;
        else if (entity.HasComponent<DirectionalLightComponent>()) icon = ICON_FA_LIGHTBULB; 
        else if (entity.HasComponent<PointLightComponent>()) icon = ICON_FA_LIGHTBULB; 

        std::string displayString = icon + " " + tag;

        // 1. 判断是否在多选列表中
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

        // ==========================================
        // 支持 Shift + 左键范围选择
        // 支持 Ctrl + 左键进行多选
        // ==========================================
        if (ImGui::IsItemClicked()) {
            if (ImGui::GetIO().KeyShift) {
                // 如果按住了 Shift，不要立刻操作数组，先标记下来，等全树画完后统一结算范围
                m_ShiftClickTarget = entity; 
            } else if (ImGui::GetIO().KeyCtrl) {
                ToggleEntitySelection(entity); // 按住 Ctrl：追加/取消选择
                m_LastClickedEntity = entity;  // 记录为最新的锚点
            } else {
                SetSelectedEntity(entity);     // 普通点击：单选
                m_LastClickedEntity = entity;  // 记录为最新的锚点
            }
        }

        // 拖放源
        if (ImGui::BeginDragDropSource()) {
            entt::entity entityID = entity;
            ImGui::SetDragDropPayload("ENTITY_PAYLOAD", &entityID, sizeof(entt::entity));
            ImGui::Text("%s", tag.c_str());
            ImGui::EndDragDropSource();
        }

        // 核心交互：带有拖拽重排 (Reorder) 指示线的 DropTarget 逻辑
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_PAYLOAD", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                
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

        // 右键菜单
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Duplicate Entity")) {
                // 如果右击的是被选中的物体，就把所有选中的都复制！否则只复制这一个。
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

        // 处理节点右侧的可视化按钮
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 20.0f);
        ImGui::SetCursorPosY(cursorY);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
        
        std::string eyeIcon = tagComp.IsActive ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
        
        if (!activeInHierarchy) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }

        ImGui::PushID((uint32_t)entity); 
        
        if (ImGui::Button(eyeIcon.c_str(), ImVec2(24.0f, ImGui::GetTextLineHeight()))) {
            tagComp.IsActive = !tagComp.IsActive; 
        }
        
        ImGui::PopID();

        if (!activeInHierarchy) {
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        // ==========================================
        // 处理展开节点 (修复批量删除时的检查)
        // ==========================================
        if (opened) {
            // 检查当前实体是否在即将被删除的列表中
            bool isBeingDestroyed = std::find(m_EntitiesToDestroy.begin(), m_EntitiesToDestroy.end(), entity) != m_EntitiesToDestroy.end();
            
            // 如果它没被标记为删除，才去遍历它的子节点，防止迭代器崩溃
            if (!isBeingDestroyed) {
                std::vector<entt::entity> children = rel.Children;
                for (auto childID : children) {
                    DrawEntityNode({ childID, m_Context.get() });
                }
            }
            ImGui::TreePop();
        }
    }

    void SceneHierarchyPanel::DrawComponents() {
        if (m_SelectedEntities.empty()) return;

        // ==========================================
        // 多选顶部提示 UI
        // ==========================================
        if (m_SelectedEntities.size() > 1) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 0.3f, 1.0f)); 
            ImGui::Text("Batch Editing %zu Entities", m_SelectedEntities.size());
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();
        }

        // 取第一个实体作为展示和同步的基准
        Entity referenceEntity = m_SelectedEntities[0];

        // --- 绘制 Tag 组件 ---
        bool allHaveTag = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<TagComponent>()) { allHaveTag = false; break; }
        
        if (allHaveTag) {
            auto& refTagComp = referenceEntity.GetComponent<TagComponent>();

            bool isActive = refTagComp.IsActive;
            if (ImGui::Checkbox("##IsActive", &isActive)) {
                for (auto e : m_SelectedEntities) e.GetComponent<TagComponent>().IsActive = isActive;
            }
            ImGui::SameLine();

            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, refTagComp.Tag.c_str(), sizeof(buffer) - 1);
            
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::InputText("##Tag", buffer, sizeof(buffer))) {
                for (auto e : m_SelectedEntities) e.GetComponent<TagComponent>().Tag = std::string(buffer);
            }
        }
        ImGui::Separator();

        // --- 绘制 Transform 组件 ---
        bool allHaveTransform = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<TransformComponent>()) { allHaveTransform = false; break; }

        if (allHaveTransform) {
            if (ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Transform")) {
                auto& refTransform = referenceEntity.GetComponent<TransformComponent>();

                glm::vec3 translation = refTransform.Translation;
                if (ImGui::DragFloat3("Position", glm::value_ptr(translation), 0.1f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<TransformComponent>().Translation = translation;
                }
                
                glm::vec3 rotation = glm::degrees(refTransform.Rotation);
                if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotation), 1.0f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<TransformComponent>().Rotation = glm::radians(rotation);
                }

                glm::vec3 scale = refTransform.Scale;
                if (ImGui::DragFloat3("Scale", glm::value_ptr(scale), 0.1f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<TransformComponent>().Scale = scale;
                }

                ImGui::TreePop();
            }
        }

        // --- 绘制 Sprite Renderer 组件 ---
        bool allHaveSprite = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<SpriteRendererComponent>()) { allHaveSprite = false; break; }

        if (allHaveSprite) {
            if (ImGui::TreeNodeEx((void*)typeid(SpriteRendererComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Sprite Renderer")) {
                auto& refSrc = referenceEntity.GetComponent<SpriteRendererComponent>();
                
                glm::vec4 color = refSrc.Color;
                if (ImGui::ColorEdit4("Color", glm::value_ptr(color))) {
                    for (auto e : m_SelectedEntities) e.GetComponent<SpriteRendererComponent>().Color = color;
                }

                ImGui::Spacing();
                ImGui::Text("Texture");

                ImVec2 textureSlotSize = { 64.0f, 64.0f };
                
                if (refSrc.TextureHandle != 0 && AssetManager::IsAssetHandleValid(refSrc.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(refSrc.TextureHandle);
                    ImGui::Image((ImTextureID)(intptr_t)tex->GetRendererID(), textureSlotSize, {0, 1}, {1, 0});
                } else {
                    ImGui::Button("Empty", textureSlotSize);
                }

                if (ImGui::BeginDragDropTarget()) { 
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = (const char*)payload->Data;
                        std::filesystem::path texturePath = std::filesystem::path("assets") / pathStr;

                        if (texturePath.extension() == ".png" || texturePath.extension() == ".jpg") {
                            UUID importedHandle = AssetManager::ImportAsset(texturePath);
                            if (importedHandle != 0) {
                                // 批量应用贴图
                                for (auto e : m_SelectedEntities) {
                                    e.GetComponent<SpriteRendererComponent>().TextureHandle = importedHandle;
                                }
                                AYAYA_CORE_INFO("Successfully imported and applied texture: {0}", texturePath.string());
                            }
                        } else {
                            AYAYA_CORE_WARN("Dropped file is not a supported image format!");
                        }
                    }
                    ImGui::EndDragDropTarget(); 
                }

                if (refSrc.TextureHandle != 0) {
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textureSlotSize.y * 0.5f - 10.0f);
                    if (ImGui::Button("Remove")) {
                        for (auto e : m_SelectedEntities) e.GetComponent<SpriteRendererComponent>().TextureHandle = 0;
                    }
                }

                ImGui::TreePop();
            }
        }

        // --- 绘制 Camera 组件 ---
        bool allHaveCamera = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<CameraComponent>()) { allHaveCamera = false; break; }

        if (allHaveCamera) {
            if (ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Camera")) {
                auto& refCamera = referenceEntity.GetComponent<CameraComponent>();
                
                // 1. 投影模式 (预留扩展，目前我们固定透视，但可以展示出来)
                const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
                const char* currentProjectionTypeString = projectionTypeStrings[(int)refCamera.Camera.GetProjectionType()];
                if (ImGui::BeginCombo("Projection", currentProjectionTypeString)) {
                    for (int i = 0; i < 2; i++) {
                        bool isSelected = currentProjectionTypeString == projectionTypeStrings[i];
                        if (ImGui::Selectable(projectionTypeStrings[i], isSelected)) {
                            for (auto e : m_SelectedEntities) {
                                e.GetComponent<CameraComponent>().Camera.SetProjectionType((SceneCamera::ProjectionType)i);
                            }
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

               if (refCamera.Camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective) {
                    float fov = glm::degrees(refCamera.Camera.GetPerspectiveFOV());
                    // FOV 限制在 1度 到 179度 之间，防止画面反转或崩坏
                    if (ImGui::DragFloat("FOV", &fov, 0.1f, 1.0f, 179.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetPerspectiveFOV(glm::radians(fov));
                    }
                    float nearClip = refCamera.Camera.GetPerspectiveNearClip();
                    // 近裁剪面必须大于 0
                    if (ImGui::DragFloat("Near Clip", &nearClip, 0.1f, 0.01f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetPerspectiveNearClip(nearClip);
                    }
                    float farClip = refCamera.Camera.GetPerspectiveFarClip();
                    // 远裁剪面必须大于近裁剪面
                    if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, nearClip + 0.1f, 10000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetPerspectiveFarClip(farClip);
                    }
                } else {
                    float orthoSize = refCamera.Camera.GetOrthographicSize();
                    // 正交大小必须大于 0
                    if (ImGui::DragFloat("Size", &orthoSize, 0.1f, 0.1f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetOrthographicSize(orthoSize);
                    }
                    float nearClip = refCamera.Camera.GetOrthographicNearClip();
                    if (ImGui::DragFloat("Near Clip", &nearClip, 0.1f, -1000.0f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetOrthographicNearClip(nearClip);
                    }
                    float farClip = refCamera.Camera.GetOrthographicFarClip();
                    if (ImGui::DragFloat("Far Clip", &farClip, 0.1f, nearClip + 0.1f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetOrthographicFarClip(farClip);
                    }
                }

                // 2. 清除模式 (Clear Flags)
                const char* clearFlagStrings[] = { "Skybox", "Solid Color" };
                const char* currentClearFlagString = clearFlagStrings[(int)refCamera.ClearFlag];
                if (ImGui::BeginCombo("Clear Flags", currentClearFlagString)) {
                    for (int i = 0; i < 2; i++) {
                        bool isSelected = currentClearFlagString == clearFlagStrings[i];
                        if (ImGui::Selectable(clearFlagStrings[i], isSelected)) {
                            for (auto e : m_SelectedEntities) {
                                e.GetComponent<CameraComponent>().ClearFlag = (CameraComponent::ClearFlags)i;
                            }
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // 3. 动态展示背景颜色选择器
                if (refCamera.ClearFlag == CameraComponent::ClearFlags::SolidColor) {
                    glm::vec4 bgColor = refCamera.BackgroundColor;
                    if (ImGui::ColorEdit4("Background", glm::value_ptr(bgColor))) {
                        for (auto e : m_SelectedEntities) {
                            e.GetComponent<CameraComponent>().BackgroundColor = bgColor;
                        }
                    }
                }

                ImGui::Separator();

                // 4. 其他基础属性
                bool primary = refCamera.Primary;
                if (ImGui::Checkbox("Primary Camera", &primary)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Primary = primary;
                    if (primary) {
                        auto view = m_Context->Reg().view<CameraComponent>();
                        for (auto entityID : view) {
                            // 确保选中的物体是唯一的 primary（防止多选时多个物体变成 Primary）
                            if (!IsEntitySelected(Entity{entityID, m_Context.get()})) {
                                view.get<CameraComponent>(entityID).Primary = false;
                            }
                        }
                    }
                }
                
                bool fixedAspect = refCamera.FixedAspectRatio;
                if (ImGui::Checkbox("Fixed Aspect Ratio", &fixedAspect)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().FixedAspectRatio = fixedAspect;
                }

                float ev100 = refCamera.EV100;
                if (ImGui::DragFloat("EV100 (Exposure)", &ev100, 0.1f, -10.0f, 25.0f, "%.2f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().EV100 = ev100;
                }

                ImGui::TreePop();
            }
        }

        // --- 绘制 Directional Light 组件 ---
        bool allHaveDirLight = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<DirectionalLightComponent>()) { allHaveDirLight = false; break; }

        if (allHaveDirLight) {
            if (ImGui::TreeNodeEx((void*)typeid(DirectionalLightComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Directional Light")) {
                auto& refDlc = referenceEntity.GetComponent<DirectionalLightComponent>();
                
                glm::vec3 color = refDlc.Color;
                if (ImGui::ColorEdit3("Light Color", glm::value_ptr(color))) {
                    for (auto e : m_SelectedEntities) e.GetComponent<DirectionalLightComponent>().Color = color;
                }

                float illuminance = refDlc.Illuminance;
                if (ImGui::DragFloat("Illuminance (Lux)", &illuminance, 1000.0f, 0.0f, 150000.0f, "%.0f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<DirectionalLightComponent>().Illuminance = illuminance;
                }

                float ambient = refDlc.AmbientStrength;
                if (ImGui::DragFloat("Ambient (Sky) Light", &ambient, 100.0f, 0.0f, 50000.0f, "%.0f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<DirectionalLightComponent>().AmbientStrength = ambient;
                }

                ImGui::TreePop();
            }
        }

        // --- 绘制 Point Light 组件 ---
        bool allHavePointLight = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<PointLightComponent>()) { allHavePointLight = false; break; }

        if (allHavePointLight) {
            if (ImGui::TreeNodeEx((void*)typeid(PointLightComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Point Light")) {
                auto& refPlc = referenceEntity.GetComponent<PointLightComponent>();
                
                glm::vec3 color = refPlc.Color;
                if (ImGui::ColorEdit3("Color", glm::value_ptr(color))) {
                    for (auto e : m_SelectedEntities) e.GetComponent<PointLightComponent>().Color = color;
                }

                float power = refPlc.LuminousPower;
                if (ImGui::DragFloat("Luminous Power (lm)", &power, 50.0f, 0.0f, 100000.0f, "%.0f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<PointLightComponent>().LuminousPower = power;
                }

                ImGui::TreePop();
            }
        }

        // --- 绘制 Mesh Renderer 组件 ---
        bool allHaveMeshRenderer = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<MeshRendererComponent>()) { allHaveMeshRenderer = false; break; }

        if (allHaveMeshRenderer) {
            bool opened = ImGui::TreeNodeEx((void*)typeid(MeshRendererComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Mesh Renderer");
            
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) {
                    removeComponent = true;
                }
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refMrc = referenceEntity.GetComponent<MeshRendererComponent>();

                // --- 1. 模型资产管理 ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Model", ImGuiTreeNodeFlags_DefaultOpen)) {
                    
                    ImGui::Text("Mesh Source");
                    std::string modelDisplay = (refMrc.ModelAsset && !refMrc.ModelAsset->GetPath().empty()) 
                                                ? refMrc.ModelAsset->GetPath() : "Drop .obj / .fbx here";
                    
                    ImGui::Button(modelDisplay.c_str(), ImVec2(-1.0f, 30.0f));

                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            const char* pathStr = (const char*)payload->Data;
                            std::filesystem::path modelPath = std::filesystem::path("assets") / pathStr;
                            if (modelPath.extension() == ".obj" || modelPath.extension() == ".fbx" || modelPath.extension() == ".gltf") {
                                // 批量应用模型
                                for (auto e : m_SelectedEntities) {
                                    e.GetComponent<MeshRendererComponent>().ModelAsset = std::make_shared<Model>(modelPath.string());
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    
                    ImGui::TreePop();
                }

                // --- 2. 材质资产管理 ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
                    
                    if (refMrc.MaterialAsset) {
                        auto& mat = refMrc.MaterialAsset;
                        
                        ImGui::Text("Material Asset (.mat)");
                        std::string matDisplay = (!mat->AssetPath.empty()) ? mat->AssetPath : "Default / Internal";
                        ImGui::Button(matDisplay.c_str(), ImVec2(-1.0f, 30.0f));

                        // 批量拖拽应用材质
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                const char* pathStr = (const char*)payload->Data;
                                std::filesystem::path matPath = std::filesystem::path("assets") / pathStr;
                                if (matPath.extension() == ".mat") {
                                    for (auto e : m_SelectedEntities) {
                                        auto newMat = std::make_shared<Material>();
                                        if (MaterialSerializer::Deserialize(newMat, matPath.string())) {
                                            e.GetComponent<MeshRendererComponent>().MaterialAsset = newMat; 
                                        }
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (ImGui::Button("Save to .mat")) {
                            if (mat->AssetPath.empty() || mat->AssetPath.find("assets/Editor/") != std::string::npos) {
                                if (!std::filesystem::exists("assets/materials")) {
                                    std::filesystem::create_directories("assets/materials");
                                }
                                
                                std::string baseName = mat->Name;
                                if (baseName == "Empty Material" || baseName.empty() || baseName.find("(Instance)") != std::string::npos) {
                                    baseName = "NewMaterial";
                                }
                                
                                std::string finalPath = "assets/materials/" + baseName + ".mat";
                                int index = 1;
                                while (std::filesystem::exists(finalPath)) {
                                    finalPath = "assets/materials/" + baseName + " (" + std::to_string(index) + ").mat";
                                    index++;
                                }
                                
                                mat->AssetPath = finalPath;
                                mat->Name = std::filesystem::path(finalPath).stem().string();
                            }
                            
                            MaterialSerializer::Serialize(mat, mat->AssetPath);
                            AYAYA_CORE_INFO("Material saved to {0}", mat->AssetPath);
                        }
                        
                        ImGui::SameLine();
                        if (ImGui::Button("Remove Material")) {
                            for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().MaterialAsset = nullptr;
                        }

                        if (refMrc.MaterialAsset) { 
                            ImGui::Text("Shader: %s", refMrc.MaterialAsset->ShaderName.c_str());
                            ImGui::Separator();

                            ImGui::Columns(2, "MaterialProperties", false);
                            ImGui::SetColumnWidth(0, 140.0f); 

                            std::string lastCategory = ""; 

                            for (auto& prop : refMrc.MaterialAsset->Properties) {
                                
                                std::string currentCategory = "Other";
                                if (prop.UniformName.find("Albedo") != std::string::npos) currentCategory = "Albedo";
                                else if (prop.UniformName.find("Metallic") != std::string::npos) currentCategory = "Metallic";
                                else if (prop.UniformName.find("Roughness") != std::string::npos) currentCategory = "Roughness";
                                else if (prop.UniformName.find("Normal") != std::string::npos) currentCategory = "Normal";
                                else if (prop.UniformName.find("Emission") != std::string::npos || prop.UniformName.find("Emissive") != std::string::npos) currentCategory = "Emission";
                                else if (prop.UniformName.find("AO") != std::string::npos || prop.UniformName.find("Ambient") != std::string::npos) currentCategory = "AO";

                                if (currentCategory != lastCategory) {
                                    if (!lastCategory.empty()) ImGui::Separator();
                                    lastCategory = currentCategory;
                                }

                                ImGui::PushID(prop.UniformName.c_str()); 
                                ImGui::AlignTextToFramePadding(); 
                                ImGui::Text("%s", prop.DisplayName.c_str());
                                ImGui::NextColumn();
                                ImGui::SetNextItemWidth(-1.0f); 
                                
                                bool propChanged = false;

                                switch (prop.Type) {
                                    case MaterialPropertyType::Float:
                                        propChanged = ImGui::SliderFloat("##val", &prop.FloatValue, 0.0f, 1.0f);
                                        break;
                                    case MaterialPropertyType::Int:
                                        propChanged = ImGui::InputInt("##val", &prop.IntValue);
                                        break;
                                    case MaterialPropertyType::Bool:
                                        propChanged = ImGui::Checkbox("##val", &prop.BoolValue);
                                        break;
                                    case MaterialPropertyType::Vec2:
                                        propChanged = ImGui::DragFloat2("##val", glm::value_ptr(prop.Vec2Value), 0.05f);
                                        break;
                                    case MaterialPropertyType::Vec3:
                                        propChanged = ImGui::ColorEdit3("##val", glm::value_ptr(prop.Vec3Value), ImGuiColorEditFlags_NoInputs);
                                        break;
                                    case MaterialPropertyType::Vec4:
                                        propChanged = ImGui::ColorEdit4("##val", glm::value_ptr(prop.Vec4Value), ImGuiColorEditFlags_NoInputs);
                                        break;
                                    case MaterialPropertyType::Texture2D:
                                    {
                                        ImVec2 textureSlotSize = { 64.0f, 64.0f }; 
                                        
                                        if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                            auto tex = AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                                            ImGui::Image((ImTextureID)(intptr_t)tex->GetRendererID(), textureSlotSize, {0, 1}, {1, 0});
                                        } else {
                                            ImGui::Button("Null", textureSlotSize);
                                        }

                                        if (ImGui::BeginDragDropTarget()) {
                                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                                const char* pathStr = (const char*)payload->Data;
                                                std::filesystem::path texturePath = std::filesystem::path("assets") / pathStr;
                                                if (texturePath.extension() == ".png" || texturePath.extension() == ".jpg") {
                                                    UUID importedHandle = AssetManager::ImportAsset(texturePath);
                                                    if (importedHandle != 0) {
                                                        prop.TextureHandle = importedHandle;
                                                        prop.TexturePath = texturePath.string(); 
                                                        propChanged = true;
                                                    }
                                                }
                                            }
                                            ImGui::EndDragDropTarget();
                                        }

                                        if (prop.TextureHandle != 0) {
                                            ImGui::SameLine();
                                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textureSlotSize.y * 0.5f - 12.0f);
                                            if (ImGui::Button("X##Remove")) {
                                                prop.TextureHandle = 0;
                                                prop.TexturePath = "";
                                                propChanged = true;
                                            }
                                        }
                                        break;
                                    }
                                    default:
                                        break;
                                }

                                // 批量应用材质参数
                                if (propChanged) {
                                    for (auto e : m_SelectedEntities) {
                                        if (e.HasComponent<MeshRendererComponent>()) {
                                            auto currentMat = e.GetComponent<MeshRendererComponent>().MaterialAsset;
                                            if (currentMat) {
                                                for (auto& p : currentMat->Properties) {
                                                    if (p.UniformName == prop.UniformName) {
                                                        p = prop; // 覆盖整个属性
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                ImGui::NextColumn(); 
                                ImGui::PopID();
                            }
                            
                            ImGui::Columns(1);
                        }
                    } 
                    else {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "Warning: No Material Assigned!");
                        if (ImGui::Button("Add Default Material", ImVec2(-1.0f, 30.0f))) {
                            for (auto e : m_SelectedEntities) {
                                auto templateMat = std::make_shared<Material>();
                                if (MaterialSerializer::Deserialize(templateMat, "assets/Editor/materials/DefaultPBR.mat")) {
                                    e.GetComponent<MeshRendererComponent>().MaterialAsset = templateMat->Clone();
                                } else {
                                    e.GetComponent<MeshRendererComponent>().MaterialAsset = std::make_shared<Material>();
                                }
                            }
                        }
                    }
                    ImGui::TreePop(); 
                }
                ImGui::TreePop();
            }

            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<MeshRendererComponent>();
            }
        }

        // --- 绘制 Rigidbody 2D 组件 ---
        bool allHaveRb2d = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<Rigidbody2DComponent>()) { allHaveRb2d = false; break; }

        if (allHaveRb2d) {
            bool opened = ImGui::TreeNodeEx((void*)typeid(Rigidbody2DComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Rigidbody 2D");
            
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refRb2d = referenceEntity.GetComponent<Rigidbody2DComponent>();

                const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
                const char* currentBodyTypeString = bodyTypeStrings[(int)refRb2d.Type];
                if (ImGui::BeginCombo("Body Type", currentBodyTypeString)) {
                    for (int i = 0; i < 3; i++) {
                        bool isSelected = currentBodyTypeString == bodyTypeStrings[i];
                        if (ImGui::Selectable(bodyTypeStrings[i], isSelected)) {
                            for (auto e : m_SelectedEntities) {
                                e.GetComponent<Rigidbody2DComponent>().Type = (Rigidbody2DComponent::BodyType)i;
                            }
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                bool fixedRotation = refRb2d.FixedRotation;
                if (ImGui::Checkbox("Fixed Rotation", &fixedRotation)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<Rigidbody2DComponent>().FixedRotation = fixedRotation;
                }

                ImGui::TreePop();
            }
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<Rigidbody2DComponent>();
            }
        }

        // --- 绘制 BoxCollider 2D 组件 ---
        bool allHaveBc2d = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<BoxCollider2DComponent>()) { allHaveBc2d = false; break; }

        if (allHaveBc2d) {
            bool opened = ImGui::TreeNodeEx((void*)typeid(BoxCollider2DComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Box Collider 2D");
            
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refBc2d = referenceEntity.GetComponent<BoxCollider2DComponent>();

                glm::vec2 offset = refBc2d.Offset;
                if (ImGui::DragFloat2("Offset", glm::value_ptr(offset), 0.05f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Offset = offset;
                }

                glm::vec2 size = refBc2d.Size;
                if (ImGui::DragFloat2("Size", glm::value_ptr(size), 0.05f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Size = size;
                }

                float density = refBc2d.Density;
                if (ImGui::DragFloat("Density", &density, 0.01f, 0.0f, 10.0f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Density = density;
                }

                float friction = refBc2d.Friction;
                if (ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, 1.0f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Friction = friction;
                }

                float restitution = refBc2d.Restitution;
                if (ImGui::DragFloat("Restitution (Bounciness)", &restitution, 0.01f, 0.0f, 1.0f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Restitution = restitution;
                }

                ImGui::TreePop();
            }
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<BoxCollider2DComponent>();
            }
        }

        // ==========================================
        // “添加组件” 按钮 (基于第一个实体判定，给所有实体添加)
        // ==========================================
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosX(contentRegionAvailable.x * 0.5f - 60.0f);
        
        if (ImGui::Button("Add Component", ImVec2(150, 25))) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (!referenceEntity.HasComponent<CameraComponent>()) {
                if (ImGui::MenuItem("Camera")) {
                    for (auto e : m_SelectedEntities) {
                        if (!e.HasComponent<CameraComponent>()) {
                            auto& cc = e.AddComponent<CameraComponent>();
                            // ==========================================
                            // 核心修复 1：新建相机组件时，必须赋予默认的透视模式！
                            // ==========================================
                            cc.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!referenceEntity.HasComponent<MeshRendererComponent>()) {
                if (ImGui::MenuItem("Mesh Renderer")) {
                    for (auto e : m_SelectedEntities) {
                        if (!e.HasComponent<MeshRendererComponent>()) {
                            auto& mrc = e.AddComponent<MeshRendererComponent>();
                            auto templateMat = std::make_shared<Material>();
                            if (MaterialSerializer::Deserialize(templateMat, "assets/Editor/materials/DefaultPBR.mat")) {
                                mrc.MaterialAsset = templateMat->Clone();
                            } else {
                                mrc.MaterialAsset = std::make_shared<Material>();
                            }
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!referenceEntity.HasComponent<SpriteRendererComponent>()) {
                if (ImGui::MenuItem("Sprite Renderer")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<SpriteRendererComponent>()) e.AddComponent<SpriteRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!referenceEntity.HasComponent<DirectionalLightComponent>()) {
                if (ImGui::MenuItem("Directional Light")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<DirectionalLightComponent>()) e.AddComponent<DirectionalLightComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!referenceEntity.HasComponent<PointLightComponent>()) {
                if (ImGui::MenuItem("Point Light")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<PointLightComponent>()) e.AddComponent<PointLightComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            // ==========================================
            // 新增：允许用户从菜单中添加物理组件
            // ==========================================
            if (!referenceEntity.HasComponent<Rigidbody2DComponent>()) {
                if (ImGui::MenuItem("Rigidbody 2D")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<Rigidbody2DComponent>()) e.AddComponent<Rigidbody2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!referenceEntity.HasComponent<BoxCollider2DComponent>()) {
                if (ImGui::MenuItem("Box Collider 2D")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<BoxCollider2DComponent>()) e.AddComponent<BoxCollider2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            
            ImGui::EndPopup();
        }
    }

}