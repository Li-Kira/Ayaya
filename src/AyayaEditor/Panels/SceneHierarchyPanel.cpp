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
        // 2. 核心交互：支持 Ctrl + 左键进行多选
        // ==========================================
        if (ImGui::IsItemClicked()) {
            if (ImGui::GetIO().KeyCtrl) {
                ToggleEntitySelection(entity); // 按住 Ctrl：追加/取消选择
            } else {
                SetSelectedEntity(entity);     // 普通点击：单选
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
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f);
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

        // ==========================================
        // “添加组件” 按钮 (基于第一个实体判定，给所有实体添加)
        // ==========================================
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosX(contentRegionAvailable.x * 0.5f - 60.0f);
        
        if (ImGui::Button("Add Component", ImVec2(120, 25))) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (!referenceEntity.HasComponent<CameraComponent>()) {
                if (ImGui::MenuItem("Camera")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<CameraComponent>()) e.AddComponent<CameraComponent>();
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
            ImGui::EndPopup();
        }
    }

}