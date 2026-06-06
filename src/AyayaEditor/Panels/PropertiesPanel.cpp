#include "ayapch.h"
#include "PropertiesPanel.hpp"
#include "../EditorLayer.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Core/EditorCommands.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/MaterialSerializer.hpp"
#include "Renderer/AssetPreviewer.hpp"
#include "Renderer/Model.hpp"
#include "Core/VFS.hpp"
#include "Project/Project.hpp"
#include "Core/Application.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <IconsFontAwesome5.h>

namespace Ayaya {

    static std::shared_ptr<Material> CreateWhitePBR() {
        auto mat = std::make_shared<Material>();
        mat->Name = "New Material";
        mat->ShaderName = "PBR";
        
        MaterialProperty p1; p1.UniformName = "u_Albedo"; p1.DisplayName = "Albedo Color"; p1.Type = MaterialPropertyType::Vec3; p1.Vec3Value = glm::vec3(1.0f); mat->Properties.push_back(p1);
        MaterialProperty p2; p2.UniformName = "u_UseAlbedoMap"; p2.DisplayName = "Enable Albedo Map"; p2.Type = MaterialPropertyType::Bool; p2.BoolValue = false; mat->Properties.push_back(p2);
        MaterialProperty p3; p3.UniformName = "u_Metallic"; p3.DisplayName = "Metallic"; p3.Type = MaterialPropertyType::Float; p3.FloatValue = 0.0f; mat->Properties.push_back(p3);
        MaterialProperty p4; p4.UniformName = "u_UseMetallicMap"; p4.DisplayName = "Enable Metallic Map"; p4.Type = MaterialPropertyType::Bool; p4.BoolValue = false; mat->Properties.push_back(p4);
        MaterialProperty p5; p5.UniformName = "u_Roughness"; p5.DisplayName = "Roughness"; p5.Type = MaterialPropertyType::Float; p5.FloatValue = 0.5f; mat->Properties.push_back(p5);
        MaterialProperty p6; p6.UniformName = "u_UseRoughnessMap"; p6.DisplayName = "Enable Roughness Map"; p6.Type = MaterialPropertyType::Bool; p6.BoolValue = false; mat->Properties.push_back(p6);
        MaterialProperty p7; p7.UniformName = "u_UseNormalMap"; p7.DisplayName = "Enable Normal Map"; p7.Type = MaterialPropertyType::Bool; p7.BoolValue = false; mat->Properties.push_back(p7);
        MaterialProperty p8; p8.UniformName = "u_AO"; p8.DisplayName = "Ambient Occlusion"; p8.Type = MaterialPropertyType::Float; p8.FloatValue = 1.0f; mat->Properties.push_back(p8);
        MaterialProperty p9; p9.UniformName = "u_UseAOMap"; p9.DisplayName = "Enable AO Map"; p9.Type = MaterialPropertyType::Bool; p9.BoolValue = false; mat->Properties.push_back(p9);
        return mat;
    }

    PropertiesPanel::PropertiesPanel(const std::shared_ptr<Scene>& context) {
        SetContext(context);
    }

    void PropertiesPanel::SetContext(const std::shared_ptr<Scene>& context) {
        m_Context = context;
        m_SelectedEntities.clear();
        m_TextureGarbageBin.clear();
    }

    void PropertiesPanel::SetSelectedEntities(const std::vector<Entity>& selectedEntities) {
        if (m_Locked) return;
        m_SelectedEntities = selectedEntities;
        m_SelectedAsset = 0; // 切换回实体模式
    }

    void PropertiesPanel::OnImGuiRender() {
        m_TextureGarbageBin.clear();
        ImGui::Begin("Properties");

        // ==========================================
        // UE5-style Inspector Toolbar
        // ==========================================
        {
            float uiScale = ImGui::GetIO().FontGlobalScale;
            float btnSz = ImGui::GetFrameHeight();
            float toolbarH = btnSz + 4.0f * uiScale;

            ImVec2 cursor = ImGui::GetCursorPos();
            ImVec2 toolbarMin = ImGui::GetCursorScreenPos();
            ImVec2 toolbarMax(toolbarMin.x + ImGui::GetContentRegionAvail().x, toolbarMin.y + toolbarH);

            // Background
            ImGui::GetWindowDrawList()->AddRectFilled(
                toolbarMin, toolbarMax, IM_COL32(28, 28, 32, 255), 4.0f * uiScale);

            // --- Left: inspection target ---
            ImGui::SetCursorPos(ImVec2(cursor.x + 6.0f * uiScale, cursor.y + 2.0f * uiScale));
            if (m_SelectedAsset != 0) {
                AssetMetadata meta = AssetManager::GetMetadata(m_SelectedAsset);
                std::string displayName = meta.VirtualPath;
                auto pos = displayName.find_last_of("/\\");
                if (pos != std::string::npos) displayName = displayName.substr(pos + 1);

                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), ICON_FA_FILE " %s", displayName.c_str());
            } else if (!m_SelectedEntities.empty()) {
                if (m_SelectedEntities.size() == 1) {
                    auto& tag = m_SelectedEntities[0].GetComponent<TagComponent>();
                    const char* icon = ICON_FA_CUBE;
                    if (m_SelectedEntities[0].HasComponent<CanvasComponent>())       icon = ICON_FA_DESKTOP;
                    else if (m_SelectedEntities[0].HasComponent<UIImageComponent>()) icon = ICON_FA_IMAGE;
                    else if (m_SelectedEntities[0].HasComponent<UITextComponent>())  icon = ICON_FA_FONT;
                    else if (m_SelectedEntities[0].HasComponent<UIButtonComponent>())icon = ICON_FA_HAND_POINTER;
                    else if (m_SelectedEntities[0].HasComponent<CameraComponent>())       icon = ICON_FA_VIDEO;
                    else if (m_SelectedEntities[0].HasComponent<DirectionalLightComponent>()) icon = ICON_FA_SUN;
                    else if (m_SelectedEntities[0].HasComponent<PointLightComponent>())      icon = ICON_FA_LIGHTBULB;
                    ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "%s %s", icon, tag.Tag.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), ICON_FA_CUBES " %zu Entities",
                        m_SelectedEntities.size());
                }
            } else {
                ImGui::TextDisabled(ICON_FA_SEARCH " No selection");
            }

            // --- Right: lock ---
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - btnSz + cursor.x);
            ImGui::SetCursorPosY(cursor.y + 2.0f * uiScale);
            if (ImGui::Button(m_Locked ? ICON_FA_LOCK : ICON_FA_UNLOCK, ImVec2(btnSz, btnSz))) {
                m_Locked = !m_Locked;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(m_Locked ? "Inspector locked — click to unlock" : "Inspector unlocked — click to lock");

            // Advance cursor past toolbar
            ImGui::SetCursorPos(ImVec2(cursor.x, cursor.y + toolbarH + 4.0f * uiScale));
            ImGui::Separator();
        }

        if (m_SelectedAsset != 0) {
            DrawAssetInspector();
        } else if (m_SelectedEntities.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Select an Entity or Asset to inspect");
        } else {
            float uiScale = ImGui::GetIO().FontGlobalScale;
            Entity referenceEntity = m_SelectedEntities[0];

            DrawTagComponent(referenceEntity, uiScale);
            DrawTransformComponent(referenceEntity);
            DrawSpriteRendererComponent(referenceEntity, uiScale);
            DrawCameraComponent(referenceEntity);
            DrawDirectionalLightComponent(referenceEntity);
            DrawPointLightComponent(referenceEntity);
            DrawEnvironmentComponent(referenceEntity);
            DrawMeshRendererComponent(referenceEntity, uiScale);
            DrawPostProcessVolumeComponent(referenceEntity, uiScale);
            DrawLuaScriptComponent(referenceEntity);
            DrawRigidbody2DComponent(referenceEntity);
            DrawBoxCollider2DComponent(referenceEntity);
            DrawCanvasComponent(referenceEntity);
            DrawRectTransformComponent(referenceEntity, uiScale);
            DrawUIImageComponent(referenceEntity, uiScale);
            DrawUITextComponent(referenceEntity);
            DrawUIButtonComponent(referenceEntity);

            DrawAddComponentButton(referenceEntity, uiScale);
        }

        ImGui::End();
    }

    void PropertiesPanel::DrawTagComponent(Entity referenceEntity, float uiScale) {
        // ==========================================
        // --- 绘制 Tag 组件 ---
        // ==========================================
        // 记录的命令: 是否激活、名称变化
        bool allHaveTag = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<TagComponent>()) { allHaveTag = false; break; }
        
        if (allHaveTag) {
            auto& refTagComp = referenceEntity.GetComponent<TagComponent>();

            // ==========================================
            // 1. Checkbox 的撤回逻辑 (单次点击触发)
            // ==========================================
            bool isActive = refTagComp.IsActive;
            if (ImGui::Checkbox("##IsActive", &isActive)) {
                
                // 【绝妙细节 1】：根据单选/多选，以及勾选状态，动态生成命令名字！
                std::string cmdName;
                std::string actionStr = isActive ? "Enable " : "Disable ";
                if (m_SelectedEntities.size() == 1) {
                    cmdName = actionStr + "'" + refTagComp.Tag + "'"; // 例如："Disable 'Enemy'"
                } else {
                    cmdName = actionStr + std::to_string(m_SelectedEntities.size()) + " Entities"; // 例如："Enable 3 Entities"
                }

                auto macroCmd = std::make_shared<MacroCommand>(cmdName);
                
                for (auto e : m_SelectedEntities) {
                    TagComponent oldComp = e.GetComponent<TagComponent>(); 
                    TagComponent newComp = oldComp;
                    newComp.IsActive = isActive;
                    
                    e.GetComponent<TagComponent>().IsActive = isActive;
                    
                    macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<TagComponent>>(e, oldComp, newComp));
                }
                EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
            }
            
            ImGui::SameLine();

            // ==========================================
            // 2. InputText 的撤回逻辑 (持续输入状态拦截)
            // ==========================================
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, refTagComp.Tag.c_str(), sizeof(buffer) - 1);
            
            static std::vector<std::string> s_OldTags;
            
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            
            if (ImGui::InputText("##Tag", buffer, sizeof(buffer))) {
                for (auto e : m_SelectedEntities) e.GetComponent<TagComponent>().Tag = std::string(buffer);
            }

            if (ImGui::IsItemActivated()) {
                s_OldTags.clear();
                for (auto e : m_SelectedEntities) {
                    s_OldTags.push_back(e.GetComponent<TagComponent>().Tag);
                }
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                
                // 【绝妙细节 2】：精确播报名字的变更！
                std::string cmdName;
                if (m_SelectedEntities.size() == 1) {
                    // 例如："Rename 'Cube' to 'Enemy'"
                    cmdName = "Rename '" + s_OldTags[0] + "' to '" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                } else {
                    // 例如："Rename 5 Entities"
                    cmdName = "Rename " + std::to_string(m_SelectedEntities.size()) + " Entities";
                }

                auto macroCmd = std::make_shared<MacroCommand>(cmdName);
                
                for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                    Entity e = m_SelectedEntities[i];
                    
                    TagComponent oldComp = e.GetComponent<TagComponent>();
                    oldComp.Tag = s_OldTags[i]; 
                    TagComponent newComp = e.GetComponent<TagComponent>(); 
                    
                    macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<TagComponent>>(e, oldComp, newComp));
                }
                EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
            }
        }
        ImGui::Separator();
    }

    void PropertiesPanel::DrawTransformComponent(Entity referenceEntity) {
        // ==========================================
        // --- 绘制 Transform 组件 ---
        // ==========================================
        // 记录的命令: 位置、缩放、旋转变化
        bool allHaveTransform = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<TransformComponent>()) { allHaveTransform = false; break; }

        if (allHaveTransform) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            bool opened = ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_ARROWS_ALT " Transform");
            ImGui::PopFont();
            
            if (opened) {
                auto& refTransform = referenceEntity.GetComponent<TransformComponent>();

                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                static std::vector<TransformComponent> s_OldTransforms;

                // 【物理快照】：在控件绘制前，提取最纯净的上一帧状态，防止拖拽瞬间数据被篡改！
                std::vector<TransformComponent> tempTransforms;
                for (auto e : m_SelectedEntities) tempTransforms.push_back(e.GetComponent<TransformComponent>());

                bool activated = false, deactivated = false;

                // ------------------------------------------
                // 1. 绘制 Position
                // ------------------------------------------
                glm::vec3 translation = refTransform.Translation;
                if (UI::DrawVec3Control("Position", translation, 0.0f, 80.0f, &activated, &deactivated)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<TransformComponent>().Translation = translation;
                }
                
                if (activated) s_OldTransforms = tempTransforms; // 拖动开始：装填纯净的快照
                if (deactivated) { // 拖动结束：结算命令
                    auto macroCmd = std::make_shared<MacroCommand>("Change Position of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        Entity e = m_SelectedEntities[i];
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<TransformComponent>>(
                            e, s_OldTransforms[i], e.GetComponent<TransformComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                }

                // ------------------------------------------
                // 2. 绘制 Rotation
                // ------------------------------------------
                activated = false; deactivated = false;
                glm::vec3 rotation = glm::degrees(refTransform.Rotation);
                if (UI::DrawVec3Control("Rotation", rotation, 0.0f, 80.0f, &activated, &deactivated)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<TransformComponent>().Rotation = glm::radians(rotation);
                }
                
                if (activated) s_OldTransforms = tempTransforms;
                if (deactivated) {
                    auto macroCmd = std::make_shared<MacroCommand>("Change Rotation of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        Entity e = m_SelectedEntities[i];
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<TransformComponent>>(
                            e, s_OldTransforms[i], e.GetComponent<TransformComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                }

                // ------------------------------------------
                // 3. 绘制 Scale
                // ------------------------------------------
                activated = false; deactivated = false;
                glm::vec3 scale = refTransform.Scale;
                if (UI::DrawVec3Control("Scale", scale, 1.0f, 80.0f, &activated, &deactivated)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<TransformComponent>().Scale = scale;
                }
                
                if (activated) s_OldTransforms = tempTransforms;
                if (deactivated) {
                    auto macroCmd = std::make_shared<MacroCommand>("Change Scale of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        Entity e = m_SelectedEntities[i];
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<TransformComponent>>(
                            e, s_OldTransforms[i], e.GetComponent<TransformComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                }

                ImGui::TreePop();
            }
        }
    }

    void PropertiesPanel::DrawSpriteRendererComponent(Entity referenceEntity, float uiScale) {
        // ==========================================
        // --- 绘制 Sprite Renderer 组件 ---
        // ==========================================
        // 记录的命令：颜色的拖拽修改、贴图的拖入分配，以及点击按钮移除贴图
        bool allHaveSprite = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<SpriteRendererComponent>()) { allHaveSprite = false; break; }

        if (allHaveSprite) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.8f, 1.0f)); 
            bool opened = ImGui::TreeNodeEx((void*)typeid(SpriteRendererComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_IMAGE " Sprite Renderer");
            ImGui::PopStyleColor();
            ImGui::PopFont();

            // 新增：允许通过右键菜单移除组件
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refSrc = referenceEntity.GetComponent<SpriteRendererComponent>();
                
                // 动态名字生成器
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // ------------------------------------------
                // 1. Color 颜色面板 (带状态拦截)
                // ------------------------------------------
                static std::vector<SpriteRendererComponent> s_OldSprites;

                glm::vec4 color = refSrc.Color;
                if (ImGui::ColorEdit4("Color", glm::value_ptr(color))) {
                    for (auto e : m_SelectedEntities) e.GetComponent<SpriteRendererComponent>().Color = color;
                }
                
                // 颜色选择器的撤回拦截
                if (ImGui::IsItemActivated()) {
                    s_OldSprites.clear();
                    for (auto e : m_SelectedEntities) s_OldSprites.push_back(e.GetComponent<SpriteRendererComponent>());
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    auto macroCmd = std::make_shared<MacroCommand>("Change Sprite Color of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<SpriteRendererComponent>>(
                            m_SelectedEntities[i], s_OldSprites[i], m_SelectedEntities[i].GetComponent<SpriteRendererComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                }

                ImGui::Spacing();
                ImGui::Text("Texture");

                ImVec2 textureSlotSize = { 64.0f * uiScale, 64.0f * uiScale };
                
                if (refSrc.TextureHandle != 0 && AssetManager::IsAssetHandleValid(refSrc.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(refSrc.TextureHandle);
                    if (tex && tex->GetImGuiTextureID() != nullptr) {
                        ImVec2 uv0 = tex->IsDataFlipped() ? ImVec2(0, 1) : ImVec2(0, 0);
                        ImVec2 uv1 = tex->IsDataFlipped() ? ImVec2(1, 0) : ImVec2(1, 1);
                        ImGui::Image((ImTextureID)tex->GetImGuiTextureID(), textureSlotSize, uv0, uv1);
                    } else {
                        ImGui::Button("Loading...", textureSlotSize);
                    }
                } else {
                    ImGui::Button("Empty", textureSlotSize);
                }

                // ------------------------------------------
                // 2. 贴图拖放逻辑 (带撤回打包)
                // ------------------------------------------
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        UUID droppedHandle = *(const UUID*)payload->Data;
                        auto meta = AssetManager::GetMetadata(droppedHandle);
                        if (droppedHandle != 0 && meta.Type == AssetType::Texture2D) {
                            if (!meta.VirtualPath.empty())
                                AssetManager::ImportAsset(VFS::ResolveString(meta.VirtualPath));
                            std::vector<SpriteRendererComponent> oldComps;
                            for (auto e : m_SelectedEntities) oldComps.push_back(e.GetComponent<SpriteRendererComponent>());
                            for (auto e : m_SelectedEntities) {
                                e.GetComponent<SpriteRendererComponent>().TextureHandle = droppedHandle;
                            }
                            auto macroCmd = std::make_shared<MacroCommand>("Assign Sprite Texture to " + getTargetName());
                            for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                                macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<SpriteRendererComponent>>(
                                    m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<SpriteRendererComponent>()
                                ));
                            }
                            EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                            AYAYA_CORE_INFO("Applied texture via UUID: {0}", (uint64_t)droppedHandle);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // ------------------------------------------
                // 3. 移除贴图逻辑 (带撤回打包)
                // ------------------------------------------
                if (refSrc.TextureHandle != 0) {
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textureSlotSize.y * 0.5f - 10.0f * uiScale);
                    if (ImGui::Button("Remove")) {
                        // 【撤回拦截】：点击瞬间，备份旧状态
                        std::vector<SpriteRendererComponent> oldComps;
                        for (auto e : m_SelectedEntities) oldComps.push_back(e.GetComponent<SpriteRendererComponent>());

                        // 执行移除
                        for (auto e : m_SelectedEntities) e.GetComponent<SpriteRendererComponent>().TextureHandle = 0;

                        // 提交撤回命令
                        auto macroCmd = std::make_shared<MacroCommand>("Remove Sprite Texture of " + getTargetName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<SpriteRendererComponent>>(
                                m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<SpriteRendererComponent>()
                            ));
                        }
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                }

                ImGui::TreePop();
            }

            // 处理右键移除整个组件的结算
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<SpriteRendererComponent>();
            }
        }
    }

    void PropertiesPanel::DrawCameraComponent(Entity referenceEntity) {
        // ==========================================
        // --- 绘制 Camera 组件 ---
        // ==========================================
        // 记录的命令：投影模式的切换、FOV与裁剪面的拖拽修改、清除模式的切换、背景颜色的修改、主相机(Primary)的切换(包含全局相机状态备份)、固定宽高比的切换、曝光度(EV100)的拖拽修改
        bool allHaveCamera = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<CameraComponent>()) { allHaveCamera = false; break; }

        if (allHaveCamera) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.6f, 0.9f, 1.0f)); 
            bool opened = ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_VIDEO " Camera");
            ImGui::PopStyleColor();
            ImGui::PopFont();

            // 新增：右键移除组件支持
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refCamera = referenceEntity.GetComponent<CameraComponent>();
                
                // 动态名字生成器
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // 【绝妙细节】：定义一个复用的 Lambda，处理所有拖拽/颜色条的撤回逻辑
                std::vector<CameraComponent> pureOldCameras;
                for (auto e : m_SelectedEntities) pureOldCameras.push_back(e.GetComponent<CameraComponent>());
                
                static std::vector<CameraComponent> s_OldCameras;
                auto handleDragState = [&](const std::string& actionName) {
                    if (ImGui::IsItemActivated()) {
                        s_OldCameras = pureOldCameras;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        auto macroCmd = std::make_shared<MacroCommand>(actionName + " of " + getTargetName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<CameraComponent>>(
                                m_SelectedEntities[i], s_OldCameras[i], m_SelectedEntities[i].GetComponent<CameraComponent>()
                            ));
                        }
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                };
                
                // 1. 投影模式 (下拉框，瞬时状态)
                const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
                const char* currentProjectionTypeString = projectionTypeStrings[(int)refCamera.Camera.GetProjectionType()];
                if (ImGui::BeginCombo("Projection", currentProjectionTypeString)) {
                    for (int i = 0; i < 2; i++) {
                        bool isSelected = currentProjectionTypeString == projectionTypeStrings[i];
                        if (ImGui::Selectable(projectionTypeStrings[i], isSelected)) {
                            // 【拦截】：下拉框选项改变瞬间
                            std::vector<CameraComponent> oldComps;
                            for (auto e : m_SelectedEntities) oldComps.push_back(e.GetComponent<CameraComponent>());

                            for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetProjectionType((SceneCamera::ProjectionType)i);

                            auto macroCmd = std::make_shared<MacroCommand>("Change Camera Projection of " + getTargetName());
                            for (size_t j = 0; j < m_SelectedEntities.size(); ++j) {
                                macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<CameraComponent>>(
                                    m_SelectedEntities[j], oldComps[j], m_SelectedEntities[j].GetComponent<CameraComponent>()
                                ));
                            }
                            EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

               if (refCamera.Camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective) {
                    float fov = glm::degrees(refCamera.Camera.GetPerspectiveFOV());
                    if (ImGui::DragFloat("FOV", &fov, 0.1f, 1.0f, 179.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetPerspectiveFOV(glm::radians(fov));
                    }
                    handleDragState("Change FOV"); // 调用复用逻辑

                    float nearClip = refCamera.Camera.GetPerspectiveNearClip();
                    if (ImGui::DragFloat("Near Clip", &nearClip, 0.1f, 0.01f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetPerspectiveNearClip(nearClip);
                    }
                    handleDragState("Change Near Clip");

                    float farClip = refCamera.Camera.GetPerspectiveFarClip();
                    if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, nearClip + 0.1f, 10000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetPerspectiveFarClip(farClip);
                    }
                    handleDragState("Change Far Clip");

                } else {
                    float orthoSize = refCamera.Camera.GetOrthographicSize();
                    if (ImGui::DragFloat("Size", &orthoSize, 0.1f, 0.1f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetOrthographicSize(orthoSize);
                    }
                    handleDragState("Change Ortho Size");

                    float nearClip = refCamera.Camera.GetOrthographicNearClip();
                    if (ImGui::DragFloat("Near Clip", &nearClip, 0.1f, -1000.0f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetOrthographicNearClip(nearClip);
                    }
                    handleDragState("Change Near Clip");

                    float farClip = refCamera.Camera.GetOrthographicFarClip();
                    if (ImGui::DragFloat("Far Clip", &farClip, 0.1f, nearClip + 0.1f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetOrthographicFarClip(farClip);
                    }
                    handleDragState("Change Far Clip");
                }

                // 2. 清除模式 (Combo)
                const char* clearFlagStrings[] = { "Skybox", "Solid Color" };
                const char* currentClearFlagString = clearFlagStrings[(int)refCamera.ClearFlag];
                if (ImGui::BeginCombo("Clear Flags", currentClearFlagString)) {
                    for (int i = 0; i < 2; i++) {
                        bool isSelected = currentClearFlagString == clearFlagStrings[i];
                        if (ImGui::Selectable(clearFlagStrings[i], isSelected)) {
                            std::vector<CameraComponent> oldComps;
                            for (auto e : m_SelectedEntities) oldComps.push_back(e.GetComponent<CameraComponent>());

                            for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().ClearFlag = (CameraComponent::ClearFlags)i;

                            auto macroCmd = std::make_shared<MacroCommand>("Change Clear Flags of " + getTargetName());
                            for (size_t j = 0; j < m_SelectedEntities.size(); ++j) {
                                macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<CameraComponent>>(
                                    m_SelectedEntities[j], oldComps[j], m_SelectedEntities[j].GetComponent<CameraComponent>()
                                ));
                            }
                            EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // 3. 背景颜色选择器
                if (refCamera.ClearFlag == CameraComponent::ClearFlags::SolidColor) {
                    glm::vec4 bgColor = refCamera.BackgroundColor;
                    if (ImGui::ColorEdit4("Background", glm::value_ptr(bgColor))) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().BackgroundColor = bgColor;
                    }
                    handleDragState("Change Background Color");
                }

                ImGui::Separator();

                // 4. 其他基础属性
                bool primary = refCamera.Primary;
                if (ImGui::Checkbox("Primary Camera", &primary)) {
                    // 【蝴蝶效应处理】：因为会影响到其他未被选中的相机，我们需要备份场景中所有的相机！
                    auto view = m_Context->Reg().view<CameraComponent>();
                    std::vector<Entity> affectedEntities;
                    std::vector<CameraComponent> oldComps;

                    for (auto entityID : view) {
                        Entity e{entityID, m_Context.get()};
                        affectedEntities.push_back(e);
                        oldComps.push_back(e.GetComponent<CameraComponent>());
                    }

                    // 应用修改
                    for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Primary = primary;
                    if (primary) {
                        for (auto entityID : view) {
                            Entity e{entityID, m_Context.get()};
                            bool isSel = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), e) != m_SelectedEntities.end();
                            if (!isSel) e.GetComponent<CameraComponent>().Primary = false;
                        }
                    }

                    // 提交包含所有相机的撤回指令
                    auto macroCmd = std::make_shared<MacroCommand>("Toggle Primary Camera");
                    for (size_t i = 0; i < affectedEntities.size(); ++i) {
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<CameraComponent>>(
                            affectedEntities[i], oldComps[i], affectedEntities[i].GetComponent<CameraComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                }
                
                bool fixedAspect = refCamera.FixedAspectRatio;
                if (ImGui::Checkbox("Fixed Aspect Ratio", &fixedAspect)) {
                    std::vector<CameraComponent> oldComps;
                    for (auto e : m_SelectedEntities) oldComps.push_back(e.GetComponent<CameraComponent>());
                    
                    for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().FixedAspectRatio = fixedAspect;
                    
                    auto macroCmd = std::make_shared<MacroCommand>("Toggle Fixed Aspect Ratio of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<CameraComponent>>(
                            m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<CameraComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                }

                float ev100 = refCamera.EV100;
                if (ImGui::DragFloat("EV100 (Exposure)", &ev100, 0.1f, -10.0f, 25.0f, "%.2f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().EV100 = ev100;
                }
                handleDragState("Change Exposure");

                ImGui::TreePop();
            }

            // 处理组件移除的结算
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<CameraComponent>();
            }
        }
    }

    void PropertiesPanel::DrawDirectionalLightComponent(Entity referenceEntity) {
        // ==========================================
        // --- 绘制 Directional Light 组件 ---
        // ==========================================
        // 记录的命令：颜色的拖拽修改、光照强度的拖拽修改
        bool allHaveDirLight = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<DirectionalLightComponent>()) { allHaveDirLight = false; break; }

        if (allHaveDirLight) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.2f, 1.0f)); 
            bool opened = ImGui::TreeNodeEx((void*)typeid(DirectionalLightComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_SUN " Directional Light");
            ImGui::PopStyleColor();
            ImGui::PopFont();

            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refDlc = referenceEntity.GetComponent<DirectionalLightComponent>();
                
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // ==========================================
                // 【绝妙修复】：在任何 UI 交互发生前，先拍下一张绝对纯净的“快照”！
                // ==========================================
                std::vector<DirectionalLightComponent> pureOldLights;
                for (auto e : m_SelectedEntities) pureOldLights.push_back(e.GetComponent<DirectionalLightComponent>());

                static std::vector<DirectionalLightComponent> s_OldLights;
                auto handleDragState = [&](const std::string& actionName) {
                    if (ImGui::IsItemActivated()) {
                        // 使用上面拍好的纯净快照，而不是去读可能已经被 UI 污染的实体数据！
                        s_OldLights = pureOldLights; 
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        auto macroCmd = std::make_shared<MacroCommand>(actionName + " of " + getTargetName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<DirectionalLightComponent>>(
                                m_SelectedEntities[i], s_OldLights[i], m_SelectedEntities[i].GetComponent<DirectionalLightComponent>()
                            ));
                        }
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                };
                
                glm::vec3 color = refDlc.Color;
                if (ImGui::ColorEdit3("Light Color", glm::value_ptr(color))) {
                    for (auto e : m_SelectedEntities) e.GetComponent<DirectionalLightComponent>().Color = color;
                }
                handleDragState("Change Light Color");

                float illuminance = refDlc.Illuminance;
                if (ImGui::DragFloat("Illuminance (Lux)", &illuminance, 1000.0f, 0.0f, 150000.0f, "%.0f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<DirectionalLightComponent>().Illuminance = illuminance;
                }
                handleDragState("Change Illuminance");

                ImGui::TreePop();
            }

            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<DirectionalLightComponent>();
            }
        }
    }

    void PropertiesPanel::DrawPointLightComponent(Entity referenceEntity) {
        // ==========================================
        // --- 绘制 Point Light 组件 ---
        // ==========================================
        // 记录的命令：颜色的拖拽修改、发光功率(Luminous Power)的拖拽修改
        bool allHavePointLight = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<PointLightComponent>()) { allHavePointLight = false; break; }

        if (allHavePointLight) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.6f, 0.1f, 1.0f)); 
            bool opened = ImGui::TreeNodeEx((void*)typeid(PointLightComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_LIGHTBULB " Point Light");
            ImGui::PopStyleColor();
            ImGui::PopFont();

            // 新增：右键移除组件支持
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refPlc = referenceEntity.GetComponent<PointLightComponent>();
                
                // 动态名字生成器
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // ==========================================
                // 【物理快照】：在 UI 渲染前捕获绝对纯净的旧状态
                // ==========================================
                std::vector<PointLightComponent> pureOldLights;
                for (auto e : m_SelectedEntities) pureOldLights.push_back(e.GetComponent<PointLightComponent>());

                static std::vector<PointLightComponent> s_OldLights;
                auto handleDragState = [&](const std::string& actionName) {
                    if (ImGui::IsItemActivated()) {
                        s_OldLights = pureOldLights; // 激活瞬间载入纯净快照
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        auto macroCmd = std::make_shared<MacroCommand>(actionName + " of " + getTargetName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<PointLightComponent>>(
                                m_SelectedEntities[i], s_OldLights[i], m_SelectedEntities[i].GetComponent<PointLightComponent>()
                            ));
                        }
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                };
                
                // 1. 颜色修改与状态拦截
                glm::vec3 color = refPlc.Color;
                if (ImGui::ColorEdit3("Color", glm::value_ptr(color))) {
                    for (auto e : m_SelectedEntities) e.GetComponent<PointLightComponent>().Color = color;
                }
                handleDragState("Change Light Color");

                // 2. 发光功率修改与状态拦截
                float power = refPlc.LuminousPower;
                if (ImGui::DragFloat("Luminous Power (lm)", &power, 50.0f, 0.0f, 100000.0f, "%.0f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<PointLightComponent>().LuminousPower = power;
                }
                handleDragState("Change Luminous Power");

                float radius = refPlc.Radius;
                if (ImGui::DragFloat("Radius (m)", &radius, 0.1f, 0.1f, 1000.0f, "%.1f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<PointLightComponent>().Radius = radius;
                }
                handleDragState("Change PointLight Radius");

                float falloff = refPlc.Falloff;
                if (ImGui::DragFloat("Falloff", &falloff, 0.05f, 0.0f, 10.0f, "%.2f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<PointLightComponent>().Falloff = falloff;
                }
                handleDragState("Change PointLight Falloff");

                ImGui::TreePop();
            }

            // 处理组件移除的结算
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<PointLightComponent>();
            }
        }
    }

    void PropertiesPanel::DrawEnvironmentComponent(Entity referenceEntity) {
        // ==========================================
        // --- 绘制 Environment (天空盒) 组件 ---
        // ==========================================
        // 记录的命令：天空盒类型切换、环境光亮度拖拽修改、HDR/LDR全景贴图的分配、传统六面体Cubemap各面的分配、重新烘焙触发、环境底光的拖拽修改
        bool allHaveEnvironment = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<EnvironmentComponent>()) { allHaveEnvironment = false; break; }

        if (allHaveEnvironment) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.9f, 1.0f)); 
            bool opened = ImGui::TreeNodeEx((void*)typeid(EnvironmentComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_GLOBE " Environment (Skybox)");
            ImGui::PopStyleColor();
            ImGui::PopFont();

            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refEnv = referenceEntity.GetComponent<EnvironmentComponent>();

                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                std::vector<EnvironmentComponent> pureOldEnvs;
                for (auto e : m_SelectedEntities) pureOldEnvs.push_back(e.GetComponent<EnvironmentComponent>());

                static std::vector<EnvironmentComponent> s_OldEnvs;
                auto handleDragState = [&](const std::string& actionName) {
                    if (ImGui::IsItemActivated()) {
                        s_OldEnvs = pureOldEnvs; 
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        auto macroCmd = std::make_shared<MacroCommand>(actionName + " of " + getTargetName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<EnvironmentComponent>>(
                                m_SelectedEntities[i], s_OldEnvs[i], m_SelectedEntities[i].GetComponent<EnvironmentComponent>()
                            ));
                        }
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                };

                // ------------------------------------------
                // 1. 类型切换下拉框
                // ------------------------------------------
                const char* envTypeStrings[] = { "None", "HDR Equirectangular", "LDR Equirectangular", "Classic Cubemap" };
                int currentTypeIdx = (int)refEnv.Type;
                if (ImGui::Combo("Type", &currentTypeIdx, envTypeStrings, 4)) {
                    std::vector<EnvironmentComponent> oldComps = pureOldEnvs;
                    for (auto& c : oldComps) c.IsDirty = true; // 强行“弄脏”备份数据以触发重绘

                    for (auto e : m_SelectedEntities) {
                        auto& comp = e.GetComponent<EnvironmentComponent>();
                        comp.Type = (EnvironmentType)currentTypeIdx;
                        comp.IsDirty = true; 
                    }

                    auto macroCmd = std::make_shared<MacroCommand>("Change Environment Type of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<EnvironmentComponent>>(
                            m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<EnvironmentComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                }

                // ------------------------------------------
                // 2. 环境光亮度滑块
                // ------------------------------------------
                float intensity = refEnv.Intensity;
                if (ImGui::DragFloat("Intensity", &intensity, 100.0f, 0.0f, 150000.0f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<EnvironmentComponent>().Intensity = intensity;
                }
                handleDragState("Change Environment Intensity");

                if (refEnv.Type != EnvironmentType::None) {
                    // ------------------------------------------
                    // 3. 全景图 (HDR/JPG) 资源槽位与拖拽交互
                    // ------------------------------------------
                    if (refEnv.Type == EnvironmentType::HDR_Equirectangular || refEnv.Type == EnvironmentType::LDR_Equirectangular) {
                        ImGui::Spacing();
                        ImGui::Text("Equirectangular Map");
                        
                        std::string pathDisplay = "Drop .hdr / .jpg here";
                        if (refEnv.EquirectangularHandle != 0) {
                            AssetMetadata meta = AssetManager::GetMetadata(refEnv.EquirectangularHandle);
                            const std::string& vpath = meta.VirtualPath;
                            if (!vpath.empty()) {
                                auto pos = vpath.find_last_of("/\\");
                                pathDisplay = (pos != std::string::npos) ? vpath.substr(pos + 1) : vpath;
                            } else {
                                pathDisplay = "Map Loaded (ID: " + std::to_string((uint64_t)refEnv.EquirectangularHandle) + ")";
                            }
                        }

                        if (ImGui::Button(pathDisplay.c_str(), ImVec2(-1.0f, 30.0f)))
                            ImGui::OpenPopup("EnvEquirectPopup");

                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                UUID droppedHandle = *(const UUID*)payload->Data;
                                auto meta = AssetManager::GetMetadata(droppedHandle);
                                if (droppedHandle != 0 && meta.Type == AssetType::Texture2D) {
                                    if (!meta.VirtualPath.empty())
                                        AssetManager::ImportAsset(VFS::ResolveString(meta.VirtualPath));
                                    std::vector<EnvironmentComponent> oldComps = pureOldEnvs;
                                    for (auto& c : oldComps) c.IsDirty = true;
                                    for (auto e : m_SelectedEntities) {
                                        auto& comp = e.GetComponent<EnvironmentComponent>();
                                        comp.EquirectangularHandle = droppedHandle;
                                        comp.Type = EnvironmentType::HDR_Equirectangular;
                                        comp.IsDirty = true;
                                    }

                                        auto macroCmd = std::make_shared<MacroCommand>("Assign Equirectangular Map to " + getTargetName());
                                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<EnvironmentComponent>>(
                                                m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<EnvironmentComponent>()
                                            ));
                                        }
                                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (ImGui::BeginPopup("EnvEquirectPopup")) {
                            ImGui::TextDisabled("Built-in HDR");
                            ImGui::Separator();
                            if (ImGui::MenuItem("Newport Loft")) {
                                std::filesystem::path builtinPath = VFS::ResolveString("engine://Editor/textures/skybox/hdr/newport_loft.hdr");
                                UUID importedHandle = AssetManager::ImportAsset(builtinPath);
                                if (importedHandle != 0) {
                                    std::vector<EnvironmentComponent> oldComps = pureOldEnvs;
                                    for (auto& c : oldComps) c.IsDirty = true;
                                    for (auto e : m_SelectedEntities) {
                                        auto& comp = e.GetComponent<EnvironmentComponent>();
                                        comp.EquirectangularHandle = importedHandle;
                                        comp.Type = EnvironmentType::HDR_Equirectangular;
                                        comp.IsDirty = true;
                                    }
                                    auto macroCmd = std::make_shared<MacroCommand>("Assign Built-in HDR to " + getTargetName());
                                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<EnvironmentComponent>>(
                                            m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<EnvironmentComponent>()
                                        ));
                                    }
                                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                                }
                            }
                            ImGui::EndPopup();
                        }
                    } 
                    // ------------------------------------------
                    // 4. 传统 6 面体贴图 (统一为单一资产 UUID 拖拽)
                    // ------------------------------------------
                    else if (refEnv.Type == EnvironmentType::Classic_Cubemap) {
                        ImGui::Spacing();
                        ImGui::Text("Cubemap Asset");
                        
                        std::string faceDisplay = "Drop .cube / .dds here";
                        if (refEnv.CubemapHandle != 0) {
                            AssetMetadata meta = AssetManager::GetMetadata(refEnv.CubemapHandle);
                            const std::string& vpath = meta.VirtualPath;
                            if (!vpath.empty()) {
                                auto pos = vpath.find_last_of("/\\");
                                faceDisplay = (pos != std::string::npos) ? vpath.substr(pos + 1) : vpath;
                            } else {
                                faceDisplay = "Cubemap Loaded (ID: " + std::to_string((uint64_t)refEnv.CubemapHandle) + ")";
                            }
                        }

                        if (ImGui::Button(faceDisplay.c_str(), ImVec2(-1.0f, 30.0f)))
                            ImGui::OpenPopup("EnvCubemapPopup");

                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                UUID droppedHandle = *(const UUID*)payload->Data;
                                if (droppedHandle != 0 && AssetManager::GetMetadata(droppedHandle).Type == AssetType::TextureCube) {
                                    std::vector<EnvironmentComponent> oldComps = pureOldEnvs;
                                    for (auto& c : oldComps) c.IsDirty = true;
                                    for (auto e : m_SelectedEntities) {
                                        auto& comp = e.GetComponent<EnvironmentComponent>();
                                        comp.CubemapHandle = droppedHandle;
                                        comp.IsDirty = true;
                                    }
                                    auto macroCmd = std::make_shared<MacroCommand>("Assign Cubemap Asset to " + getTargetName());
                                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<EnvironmentComponent>>(
                                            m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<EnvironmentComponent>()
                                        ));
                                    }
                                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (ImGui::BeginPopup("EnvCubemapPopup")) {
                            ImGui::TextDisabled("Built-in Skybox");
                            ImGui::Separator();
                            if (ImGui::MenuItem("Skybox 01")) {
                                std::filesystem::path builtinPath = VFS::ResolveString("engine://Editor/textures/skybox/skybox_01/sky.cube");
                                UUID importedHandle = AssetManager::ImportAsset(builtinPath);
                                if (importedHandle != 0) {
                                    std::vector<EnvironmentComponent> oldComps = pureOldEnvs;
                                    for (auto& c : oldComps) c.IsDirty = true;
                                    for (auto e : m_SelectedEntities) {
                                        auto& comp = e.GetComponent<EnvironmentComponent>();
                                        comp.CubemapHandle = importedHandle;
                                        comp.IsDirty = true;
                                    }
                                    auto macroCmd = std::make_shared<MacroCommand>("Assign Built-in Skybox to " + getTargetName());
                                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<EnvironmentComponent>>(
                                            m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<EnvironmentComponent>()
                                        ));
                                    }
                                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                                }
                            }
                            ImGui::EndPopup();
                        }
                    }
                }

                // ------------------------------------------
                // 5. 基础环境底光颜色
                // ------------------------------------------
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                ImGui::Text("Ambient Light (Flat)");
                glm::vec3 ambientColor = refEnv.AmbientColor;
                if (ImGui::ColorEdit3("Ambient Color", glm::value_ptr(ambientColor), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<EnvironmentComponent>().AmbientColor = ambientColor;
                }
                handleDragState("Change Ambient Color");

                ImGui::TreePop();
            }
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<EnvironmentComponent>();
            }
        }
    }

    void PropertiesPanel::DrawMeshRendererComponent(Entity referenceEntity, float uiScale) {
        // ==========================================
        // --- 绘制 Mesh Renderer 组件 ---
        // ==========================================
        // 记录的命令：模型(.obj/.fbx)的拖入分配、基础几何体的切换、材质(.mat)的拖入分配、材质的移除与默认材质添加、阴影投射(Cast)与接收(Receive)的切换
        // (注：材质内部属性的修改属于“资产级别(Asset)”，为保护材质共享(Batching)，暂不纳入组件级撤回栈)
        bool allHaveMeshRenderer = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<MeshRendererComponent>()) { allHaveMeshRenderer = false; break; }

        if (allHaveMeshRenderer) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.4f, 1.0f)); 
            
            // 【优化 1】：增加 AllowItemOverlap 标志，允许我们在标题栏同一行放置删除按钮
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap;
            bool opened = ImGui::TreeNodeEx((void*)typeid(MeshRendererComponent).hash_code(), flags, ICON_FA_CUBE " Mesh Renderer");
            
            ImGui::PopStyleColor();
            ImGui::PopFont();
            
            bool removeComponent = false;

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refMrc = referenceEntity.GetComponent<MeshRendererComponent>();

                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                std::vector<MeshRendererComponent> pureOldMrcs;
                for (auto e : m_SelectedEntities) pureOldMrcs.push_back(e.GetComponent<MeshRendererComponent>());

                auto commitInstantCommand = [&](const std::string& actionName, const std::vector<MeshRendererComponent>& oldComps) {
                    auto macroCmd = std::make_shared<MacroCommand>(actionName + " of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<MeshRendererComponent>>(
                            m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<MeshRendererComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                };


                // ==========================================
                // 1. 模型资产管理
                // ==========================================
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Model", ImGuiTreeNodeFlags_DefaultOpen)) {
                    
                    ImGui::Text("Mesh Source");
                    std::string modelDisplay = "Drop .obj / .fbx here";

                    if (refMrc.ModelHandle != 0) {
                        AssetMetadata meta = AssetManager::GetMetadata(refMrc.ModelHandle);
                        const std::string& vpath = meta.VirtualPath;

                        if (vpath == "Primitive::Cube")
                            modelDisplay = "Built-in: Cube";
                        else if (vpath == "Primitive::Sphere")
                            modelDisplay = "Built-in: Sphere";
                        else if (vpath == "Primitive::Plane")
                            modelDisplay = "Built-in: Plane";
                        else if (!vpath.empty()) {
                            auto pos = vpath.find_last_of("/\\");
                            modelDisplay = (pos != std::string::npos) ? vpath.substr(pos + 1) : vpath;
                        } else {
                            modelDisplay = "Model Assigned";
                        }
                    }

                    if (ImGui::Button(modelDisplay.c_str(), ImVec2(-1.0f, 30.0f))) {
                        ImGui::OpenPopup("ModelSelectionPopup");
                    }

                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            UUID droppedHandle = *(const UUID*)payload->Data;
                            if (droppedHandle != 0 && AssetManager::GetMetadata(droppedHandle).Type == AssetType::Model) {
                                    std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                                    for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().ModelHandle = droppedHandle;
                                    commitInstantCommand("Assign Model", oldComps);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (ImGui::BeginPopup("ModelSelectionPopup")) {
                        ImGui::TextDisabled("Built-in Primitives");
                        ImGui::Separator();
                        
                        if (ImGui::MenuItem("Cube")) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().ModelHandle = AssetManager::GetBuiltInCube();
                            commitInstantCommand("Assign Cube", oldComps);
                        }
                        if (ImGui::MenuItem("Sphere")) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().ModelHandle = AssetManager::GetBuiltInSphere();
                            commitInstantCommand("Assign Sphere", oldComps);
                        }
                        if (ImGui::MenuItem("Plane")) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().ModelHandle = AssetManager::GetBuiltInPlane();
                            commitInstantCommand("Assign Plane", oldComps);
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::TreePop();
                }

                // ==========================================
                // 2. 材质资产管理
                // ==========================================
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
                    
                    std::shared_ptr<Material> currentMat = nullptr;
                    if (refMrc.MaterialHandle != 0 && AssetManager::IsAssetHandleValid(refMrc.MaterialHandle)) {
                        currentMat = AssetManager::GetAsset<Material>(refMrc.MaterialHandle);
                    }

                    if (currentMat) {
                        ImGui::Text("Material Asset (.mat)");
                        
                        // 【优化 4】：直接读取材质的名称 (Name)，不显示长串 UUID
                        std::string matDisplay = currentMat->Name.empty() ? "Material Assigned" : currentMat->Name;
                        ImGui::Button(matDisplay.c_str(), ImVec2(-1.0f, 30.0f));

                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                UUID droppedHandle = *(const UUID*)payload->Data;
                                if (droppedHandle != 0 && AssetManager::GetMetadata(droppedHandle).Type == AssetType::Material) {
                                    std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                                    for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().MaterialHandle = droppedHandle;
                                    commitInstantCommand("Assign Material", oldComps);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (ImGui::Button("Save to .mat")) {
                            std::string physicalPath = AssetManager::GetAssetPhysicalPath(refMrc.MaterialHandle);
                            
                            if (physicalPath.empty() || physicalPath.find("assets/Editor/") != std::string::npos) {
                                if (!std::filesystem::exists("assets/materials")) {
                                    std::filesystem::create_directories("assets/materials");
                                }
                                std::string baseName = currentMat->Name;
                                if (baseName == "Built-in Default Material" || baseName == "Empty Material" || baseName.empty() || baseName.find("(Instance)") != std::string::npos) {
                                    baseName = "NewMaterial";
                                }
                                std::string finalPath = "assets/materials/" + baseName + ".mat";
                                int index = 1;
                                while (std::filesystem::exists(finalPath)) {
                                    finalPath = "assets/materials/" + baseName + " (" + std::to_string(index) + ").mat";
                                    index++;
                                }
                                currentMat->Name = std::filesystem::path(finalPath).stem().string();
                                
                                MaterialSerializer::Serialize(currentMat, finalPath);
                                
                                UUID newMatHandle = AssetManager::ImportAsset(finalPath);
                                if (newMatHandle != 0) {
                                    std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                                    for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().MaterialHandle = newMatHandle;
                                    commitInstantCommand("Save and Assign New Material", oldComps);
                                }
                            } else {
                                MaterialSerializer::Serialize(currentMat, physicalPath);
                            }
                        }
                        
                        ImGui::SameLine();

                        if (ImGui::Button("Remove Material")) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().MaterialHandle = 0;
                            commitInstantCommand("Remove Material", oldComps);
                        }

                        ImGui::Text("Shader: %s", currentMat->ShaderName.c_str());
                        ImGui::Separator();

                        ImGui::Columns(2, "MaterialProperties", false);
                        ImGui::SetColumnWidth(0, 140.0f * uiScale);
                        std::string lastCategory = ""; 

                        for (auto& prop : currentMat->Properties) {
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

                            switch (prop.Type) {
                                case MaterialPropertyType::Float: ImGui::SliderFloat("##val", &prop.FloatValue, 0.0f, 1.0f); break;
                                case MaterialPropertyType::Int: ImGui::InputInt("##val", &prop.IntValue); break;
                                case MaterialPropertyType::Bool: ImGui::Checkbox("##val", &prop.BoolValue); break;
                                case MaterialPropertyType::Vec2: ImGui::DragFloat2("##val", glm::value_ptr(prop.Vec2Value), 0.05f); break;
                                case MaterialPropertyType::Vec3: ImGui::ColorEdit3("##val", glm::value_ptr(prop.Vec3Value), ImGuiColorEditFlags_NoInputs); break;
                                case MaterialPropertyType::Vec4: ImGui::ColorEdit4("##val", glm::value_ptr(prop.Vec4Value), ImGuiColorEditFlags_NoInputs); break;
                                case MaterialPropertyType::Texture2D:
                                {
                                    ImVec2 textureSlotSize = { 64.0f * uiScale, 64.0f * uiScale };
                                    
                                    std::shared_ptr<Texture2D> tex = nullptr;
                                    if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                        tex = AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                                    }

                                    if (tex) {
                                        if (tex->GetImGuiTextureID() != nullptr && (uint64_t)tex->GetImGuiTextureID() != 0) {
                                            bool vk = (RendererAPI::GetAPI() == RendererAPI::API::Vulkan);
                                            ImVec2 uv0 = vk ? ImVec2(0, 0) : ImVec2(0, 1);
                                            ImVec2 uv1 = vk ? ImVec2(1, 1) : ImVec2(1, 0);
                                            ImGui::Image((ImTextureID)tex->GetImGuiTextureID(), textureSlotSize, uv0, uv1);
                                        } else {
                                            ImGui::Button("Loading...", textureSlotSize);
                                        }
                                    } else {
                                        ImGui::Button("Null", textureSlotSize);
                                    }

                                    if (ImGui::BeginDragDropTarget()) {
                                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                            UUID droppedHandle = *(const UUID*)payload->Data;
                                            auto meta = AssetManager::GetMetadata(droppedHandle);
                                            if (droppedHandle != 0 && meta.Type == AssetType::Texture2D) {
                                                // 确保纹理 UUID 持久化到注册表（防止 ContentBrowser 未刷新导致丢失）
                                                if (!meta.VirtualPath.empty()) {
                                                    AssetManager::ImportAsset(VFS::ResolveString(meta.VirtualPath));
                                                }
                                                if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                                    m_TextureGarbageBin.push_back(AssetManager::GetAsset<Texture2D>(prop.TextureHandle));
                                                }
                                                prop.TextureHandle = droppedHandle;
                                            }
                                        }
                                        ImGui::EndDragDropTarget();
                                    }

                                    if (prop.TextureHandle != 0) {
                                        ImGui::SameLine();
                                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textureSlotSize.y * 0.5f - 12.0f);
                                        if (ImGui::Button("X##Remove")) {
                                            if (AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                                m_TextureGarbageBin.push_back(AssetManager::GetAsset<Texture2D>(prop.TextureHandle));
                                            }
                                            prop.TextureHandle = 0;
                                        }
                                    }
                                    break;
                                }
                                default: break;
                            }

                            ImGui::NextColumn();
                            ImGui::PopID();
                        }

                        ImGui::Columns(1);
                    }
                    else {
                        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.6f, 1.0f), "Warning: No Material Assigned!");
                        ImGui::PopFont();
                        if (ImGui::Button("Add Default Material", ImVec2(-1.0f, 30.0f))) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) {
                                e.GetComponent<MeshRendererComponent>().MaterialHandle = AssetManager::GetBuiltInMaterial();
                            }
                            commitInstantCommand("Add Default Material", oldComps);
                        }
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    bool castShadows = refMrc.CastShadows;
                    if (ImGui::Checkbox("Cast Shadows", &castShadows)) {
                        std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                        for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().CastShadows = castShadows;
                        commitInstantCommand("Toggle Cast Shadows", oldComps);
                    }

                    bool receiveShadows = refMrc.ReceiveShadows;
                    if (ImGui::Checkbox("Receive Shadows", &receiveShadows)) {
                        std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                        for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().ReceiveShadows = receiveShadows;
                        commitInstantCommand("Toggle Receive Shadows", oldComps);
                    }

                    ImGui::TreePop(); 
                }
                ImGui::TreePop();
            }

            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<MeshRendererComponent>();
            }
        }
    }

    void PropertiesPanel::DrawPostProcessVolumeComponent(Entity referenceEntity, float uiScale) {
        // ==========================================
        // --- 绘制 Post Process Volume 组件 ---
        // ==========================================
        // 记录的命令：全局体积(Is Global)的切换、色调映射算法(Tone Mapping)的下拉切换、曝光补偿(Exposure)的拖拽修改、Bloom开关及其各项核心参数(阈值、平滑度、半径、强度)的拖拽修改、FXAA抗锯齿开关的切换
        bool allHavePPV = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<PostProcessVolumeComponent>()) { allHavePPV = false; break; }

        if (allHavePPV) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.4f, 0.9f, 1.0f)); // 优雅的紫色以区分后期特效
            bool opened = ImGui::TreeNodeEx((void*)typeid(PostProcessVolumeComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_MAGIC " Post Process Volume");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            
            // 右键菜单支持移除组件
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refPPV = referenceEntity.GetComponent<PostProcessVolumeComponent>();

                // 动态名字生成器
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // 【物理快照】：捕获绝对纯净的旧状态
                std::vector<PostProcessVolumeComponent> pureOldPPVs;
                for (auto e : m_SelectedEntities) pureOldPPVs.push_back(e.GetComponent<PostProcessVolumeComponent>());

                // 【一键打包】：用于瞬时交互状态(如 Checkbox, Combo)的撤回打包
                auto commitInstantCommand = [&](const std::string& actionName, const std::vector<PostProcessVolumeComponent>& oldComps) {
                    auto macroCmd = std::make_shared<MacroCommand>(actionName + " of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<PostProcessVolumeComponent>>(
                            m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<PostProcessVolumeComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                };

                // 【复用 Lambda】：处理连续拖拽(如 DragFloat)的状态拦截
                static std::vector<PostProcessVolumeComponent> s_OldPPVs;
                auto handleDragState = [&](const std::string& actionName) {
                    if (ImGui::IsItemActivated()) {
                        s_OldPPVs = pureOldPPVs; // 激活瞬间载入纯净快照
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        auto macroCmd = std::make_shared<MacroCommand>(actionName + " of " + getTargetName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<PostProcessVolumeComponent>>(
                                m_SelectedEntities[i], s_OldPPVs[i], m_SelectedEntities[i].GetComponent<PostProcessVolumeComponent>()
                            ));
                        }
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                };

                // ------------------------------------------
                // 1. 体积类型
                // ------------------------------------------
                bool isGlobal = refPPV.IsGlobal;
                if (ImGui::Checkbox("Is Global", &isGlobal)) {
                    std::vector<PostProcessVolumeComponent> oldComps = pureOldPPVs;
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().IsGlobal = isGlobal;
                    commitInstantCommand("Toggle Is Global", oldComps);
                }
                if (!refPPV.IsGlobal) {
                    ImGui::TextDisabled("Local volumes (Bounding Box Blending) coming soon...");
                }

                ImGui::Separator();
                ImGui::Text("Tone Mapping & Exposure");

                // ------------------------------------------
                // 2. 色调映射与曝光
                // ------------------------------------------
                const char* tmTypes[] = { "None", "ACES (Filmic)", "Reinhard" };
                int currentTmType = refPPV.ToneMappingType;
                if (ImGui::Combo("Algorithm", &currentTmType, tmTypes, 3)) {
                    std::vector<PostProcessVolumeComponent> oldComps = pureOldPPVs;
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().ToneMappingType = currentTmType;
                    commitInstantCommand("Change Tone Mapping Type", oldComps);
                }

                float exposure = refPPV.Exposure;
                if (ImGui::DragFloat("Exposure Comp.", &exposure, 0.05f, 0.0f, 10.0f, "%.2f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().Exposure = exposure;
                }
                handleDragState("Change Exposure");

                ImGui::Separator();
                ImGui::Text("Bloom (Dual-Filtering)");

                // ------------------------------------------
                // 3. Bloom 参数组
                // ------------------------------------------
                bool enableBloom = refPPV.EnableBloom;
                if (ImGui::Checkbox("Enable Bloom", &enableBloom)) {
                    std::vector<PostProcessVolumeComponent> oldComps = pureOldPPVs;
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().EnableBloom = enableBloom;
                    commitInstantCommand("Toggle Enable Bloom", oldComps);
                }

                if (refPPV.EnableBloom) {
                    ImGui::Indent(10.0f * uiScale);

                    float threshold = refPPV.BloomThreshold;
                    if (ImGui::DragFloat("Threshold", &threshold, 0.05f, 0.0f, 10.0f, "%.2f")) {
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().BloomThreshold = threshold;
                    }
                    handleDragState("Change Bloom Threshold");

                    float knee = refPPV.BloomKnee;
                    if (ImGui::DragFloat("Soft Knee", &knee, 0.01f, 0.0001f, 1.0f, "%.2f")) {
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().BloomKnee = knee;
                    }
                    handleDragState("Change Bloom Knee");

                    float radius = refPPV.BloomRadius;
                    if (ImGui::DragFloat("Filter Radius", &radius, 0.0005f, 0.001f, 0.02f, "%.4f")) {
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().BloomRadius = radius;
                    }
                    handleDragState("Change Bloom Radius");

                    float intensity = refPPV.BloomIntensity;
                    if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 5.0f, "%.2f")) {
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().BloomIntensity = intensity;
                    }
                    handleDragState("Change Bloom Intensity");

                    ImGui::Unindent(10.0f * uiScale);
                }

                ImGui::Separator();
                ImGui::Text("Anti-Aliasing");

                // ------------------------------------------
                // 4. FXAA
                // ------------------------------------------
                bool enableFXAA = refPPV.EnableFXAA;
                if (ImGui::Checkbox("Enable FXAA", &enableFXAA)) {
                    std::vector<PostProcessVolumeComponent> oldComps = pureOldPPVs;
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().EnableFXAA = enableFXAA;
                    commitInstantCommand("Toggle Enable FXAA", oldComps);
                }

                // ------------------------------------------
                // 5. SSAO
                // ------------------------------------------
                bool enableSSAO = refPPV.EnableSSAO;
                if (ImGui::Checkbox("Enable SSAO", &enableSSAO)) {
                    std::vector<PostProcessVolumeComponent> oldComps = pureOldPPVs;
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().EnableSSAO = enableSSAO;
                    commitInstantCommand("Toggle Enable SSAO", oldComps);
                }
                if (enableSSAO) {
                    float ssaoRadius = refPPV.SSAORadius;
                    if (ImGui::DragFloat("SSAO Radius", &ssaoRadius, 0.01f, 0.1f, 3.0f, "%.2f")) {
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().SSAORadius = ssaoRadius;
                    }
                    handleDragState("Change SSAO Radius");
                    float ssaoBias = refPPV.SSAOBias;
                    if (ImGui::DragFloat("SSAO Bias", &ssaoBias, 0.001f, 0.001f, 0.2f, "%.3f")) {
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().SSAOBias = ssaoBias;
                    }
                    handleDragState("Change SSAO Bias");
                }

                ImGui::TreePop();
            }

            // 处理组件移除结算
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<PostProcessVolumeComponent>();
            }
        }
    }

    void PropertiesPanel::DrawLuaScriptComponent(Entity referenceEntity) {
        // ==========================================
        // --- 绘制 Lua Script 组件 ---
        // ==========================================
        // 记录的命令：脚本文件(.lua)的拖入分配、手动输入脚本路径的修改
        bool allHaveLuaScript = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<LuaScriptComponent>()) { allHaveLuaScript = false; break; }

        if (allHaveLuaScript) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.5f, 1.0f)); 
            bool opened = ImGui::TreeNodeEx((void*)typeid(LuaScriptComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_FILE_CODE " Lua Script");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refLsc = referenceEntity.GetComponent<LuaScriptComponent>();

                // 动态名字生成器
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // 【物理快照】：捕获绝对纯净的旧状态
                std::vector<LuaScriptComponent> pureOldLscs;
                for (auto e : m_SelectedEntities) pureOldLscs.push_back(e.GetComponent<LuaScriptComponent>());

                ImGui::Text("Script Source");
                std::string pathDisplay = "Drop .lua file here";
                
                // 【改造】：检查 UUID 是否存在
                if (refLsc.ScriptHandle != 0) {
                    pathDisplay = "Script Loaded (ID: " + std::to_string((uint64_t)refLsc.ScriptHandle) + ")";
                }
                ImGui::Button(pathDisplay.c_str(), ImVec2(-1.0f, 30.0f));

                // ==========================================
                // 交互 1：支持拖拽 .lua 文件绑定脚本 (极简 UUID 转换)
                // ==========================================
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        UUID droppedHandle = *(const UUID*)payload->Data;
                        if (droppedHandle != 0 && AssetManager::GetMetadata(droppedHandle).Type == AssetType::LuaScript) {
                            std::vector<LuaScriptComponent> oldComps = pureOldLscs;
                            for (auto e : m_SelectedEntities) {
                                e.GetComponent<LuaScriptComponent>().ScriptHandle = droppedHandle;
                            }
                            auto macroCmd = std::make_shared<MacroCommand>("Assign Lua Script to " + getTargetName());
                            for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                                macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<LuaScriptComponent>>(
                                    m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<LuaScriptComponent>()
                                ));
                            }
                            EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::Spacing();
                
                // ==========================================
                // 交互 2：一键移除脚本 (替代原本容易越界的字符串修改)
                // ==========================================
                if (refLsc.ScriptHandle != 0) {
                    if (ImGui::Button("Remove Script", ImVec2(-1.0f, 24.0f))) {
                        std::vector<LuaScriptComponent> oldComps = pureOldLscs;
                        
                        // 清空句柄
                        for (auto e : m_SelectedEntities) {
                            e.GetComponent<LuaScriptComponent>().ScriptHandle = 0;
                        }

                        // 提交撤回指令
                        auto macroCmd = std::make_shared<MacroCommand>("Remove Lua Script from " + getTargetName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<LuaScriptComponent>>(
                                m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<LuaScriptComponent>()
                            ));
                        }
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                }

                ImGui::TreePop();
            }
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<LuaScriptComponent>();
            }
        }
    }

    void PropertiesPanel::DrawRigidbody2DComponent(Entity referenceEntity) {
        // ==========================================
        // --- 绘制 Rigidbody 2D 组件 ---
        // ==========================================
        // 记录的命令：刚体类型(Body Type)的切换、固定旋转(Fixed Rotation)的勾选
        bool allHaveRb2d = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<Rigidbody2DComponent>()) { allHaveRb2d = false; break; }

        if (allHaveRb2d) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f)); 
            bool opened = ImGui::TreeNodeEx((void*)typeid(Rigidbody2DComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_BULLSEYE " Rigidbody 2D");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refRb2d = referenceEntity.GetComponent<Rigidbody2DComponent>();

                // 动态名字生成器
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // 【物理快照】：捕获纯净状态
                std::vector<Rigidbody2DComponent> pureOldRb2ds;
                for (auto e : m_SelectedEntities) pureOldRb2ds.push_back(e.GetComponent<Rigidbody2DComponent>());

                // 【一键打包】：用于瞬时交互状态的撤回打包
                auto commitInstantCommand = [&](const std::string& actionName, const std::vector<Rigidbody2DComponent>& oldComps) {
                    auto macroCmd = std::make_shared<MacroCommand>(actionName + " of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<Rigidbody2DComponent>>(
                            m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<Rigidbody2DComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                };

                const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
                const char* currentBodyTypeString = bodyTypeStrings[(int)refRb2d.Type];
                if (ImGui::BeginCombo("Body Type", currentBodyTypeString)) {
                    for (int i = 0; i < 3; i++) {
                        bool isSelected = currentBodyTypeString == bodyTypeStrings[i];
                        if (ImGui::Selectable(bodyTypeStrings[i], isSelected)) {
                            std::vector<Rigidbody2DComponent> oldComps = pureOldRb2ds;
                            
                            for (auto e : m_SelectedEntities) {
                                e.GetComponent<Rigidbody2DComponent>().Type = (Rigidbody2DComponent::BodyType)i;
                            }
                            
                            commitInstantCommand("Change Body Type", oldComps);
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                bool fixedRotation = refRb2d.FixedRotation;
                if (ImGui::Checkbox("Fixed Rotation", &fixedRotation)) {
                    std::vector<Rigidbody2DComponent> oldComps = pureOldRb2ds;
                    
                    for (auto e : m_SelectedEntities) e.GetComponent<Rigidbody2DComponent>().FixedRotation = fixedRotation;
                    
                    commitInstantCommand("Toggle Fixed Rotation", oldComps);
                }

                ImGui::TreePop();
            }
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<Rigidbody2DComponent>();
            }
        }
    }

    void PropertiesPanel::DrawBoxCollider2DComponent(Entity referenceEntity) {
        // ==========================================
        // --- 绘制 BoxCollider 2D 组件 ---
        // ==========================================
        // 记录的命令：碰撞体偏移(Offset)、尺寸(Size)、密度(Density)、摩擦力(Friction)和恢复系数/弹性(Restitution)的拖拽修改
        bool allHaveBc2d = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<BoxCollider2DComponent>()) { allHaveBc2d = false; break; }

        if (allHaveBc2d) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.3f, 1.0f)); 
            bool opened = ImGui::TreeNodeEx((void*)typeid(BoxCollider2DComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_VECTOR_SQUARE " Box Collider 2D");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refBc2d = referenceEntity.GetComponent<BoxCollider2DComponent>();

                // 动态名字生成器
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // 【物理快照】：捕获绝对纯净的旧状态
                std::vector<BoxCollider2DComponent> pureOldBc2ds;
                for (auto e : m_SelectedEntities) pureOldBc2ds.push_back(e.GetComponent<BoxCollider2DComponent>());

                // 复用 Lambda：处理连续拖拽的状态拦截
                static std::vector<BoxCollider2DComponent> s_OldBc2ds;
                auto handleDragState = [&](const std::string& actionName) {
                    if (ImGui::IsItemActivated()) {
                        s_OldBc2ds = pureOldBc2ds; // 激活瞬间载入纯净快照
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        auto macroCmd = std::make_shared<MacroCommand>(actionName + " of " + getTargetName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<BoxCollider2DComponent>>(
                                m_SelectedEntities[i], s_OldBc2ds[i], m_SelectedEntities[i].GetComponent<BoxCollider2DComponent>()
                            ));
                        }
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                };

                glm::vec2 offset = refBc2d.Offset;
                if (ImGui::DragFloat2("Offset", glm::value_ptr(offset), 0.05f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Offset = offset;
                }
                handleDragState("Change BoxCollider2D Offset");

                glm::vec2 size = refBc2d.Size;
                if (ImGui::DragFloat2("Size", glm::value_ptr(size), 0.05f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Size = size;
                }
                handleDragState("Change BoxCollider2D Size");

                float density = refBc2d.Density;
                if (ImGui::DragFloat("Density", &density, 0.01f, 0.0f, 10.0f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Density = density;
                }
                handleDragState("Change BoxCollider2D Density");

                float friction = refBc2d.Friction;
                if (ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, 1.0f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Friction = friction;
                }
                handleDragState("Change BoxCollider2D Friction");

                float restitution = refBc2d.Restitution;
                if (ImGui::DragFloat("Restitution (Bounciness)", &restitution, 0.01f, 0.0f, 1.0f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Restitution = restitution;
                }
                handleDragState("Change BoxCollider2D Restitution");

                ImGui::TreePop();
            }
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<BoxCollider2DComponent>();
            }
        }
    }

    void PropertiesPanel::DrawCanvasComponent(Entity referenceEntity) {
        bool allHave = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<CanvasComponent>()) { allHave = false; break; }
        if (!allHave) return;

        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.75f, 0.75f, 1.0f));
        bool opened = ImGui::TreeNodeEx((void*)typeid(CanvasComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_DESKTOP " Canvas");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        bool removeComponent = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) removeComponent = true;
            ImGui::EndPopup();
        }

        if (opened) {
            auto& ref = referenceEntity.GetComponent<CanvasComponent>();
            const char* modes[] = { "Screen Overlay", "Screen Camera", "World Space" };
            int cur = (int)ref.Mode;
            if (ImGui::Combo("Render Mode", &cur, modes, 3))
                ref.Mode = (CanvasComponent::RenderMode)cur;
            ImGui::DragInt("Sort Order", &ref.SortOrder, 0.1f, -100, 100);
            ImGui::TreePop();
        }
        if (removeComponent) {
            for (auto e : m_SelectedEntities) e.RemoveComponent<CanvasComponent>();
        }
    }

    void PropertiesPanel::DrawRectTransformComponent(Entity referenceEntity, float uiScale) {
        bool allHave = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<RectTransformComponent>()) { allHave = false; break; }
        if (!allHave) return;

        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 0.9f, 1.0f));
        bool opened = ImGui::TreeNodeEx((void*)typeid(RectTransformComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_OBJECT_GROUP " Rect Transform");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        bool removeComponent = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) removeComponent = true;
            ImGui::EndPopup();
        }

        if (opened) {
            auto& ref = referenceEntity.GetComponent<RectTransformComponent>();
            // 窄面板下 DragFloat2 太宽，改用自带标签的控件，避免列布局遮挡
            float fullW = ImGui::GetContentRegionAvail().x;

            ImGui::SetNextItemWidth(fullW);
            ImGui::DragFloat2("Anchor Min", glm::value_ptr(ref.AnchorMin), 0.01f, 0.0f, 1.0f);
            ImGui::SetNextItemWidth(fullW);
            ImGui::DragFloat2("Anchor Max", glm::value_ptr(ref.AnchorMax), 0.01f, 0.0f, 1.0f);
            ImGui::SetNextItemWidth(fullW);
            ImGui::DragFloat2("Pivot", glm::value_ptr(ref.Pivot), 0.01f, 0.0f, 1.0f);
            ImGui::SetNextItemWidth(fullW);
            ImGui::DragFloat2("Position", glm::value_ptr(ref.Position), 0.5f);
            ImGui::SetNextItemWidth(fullW);
            ImGui::DragFloat2("Size##RT", glm::value_ptr(ref.Size), 0.5f, 0.0f, 10000.0f);
            ImGui::SetNextItemWidth(fullW);
            ImGui::DragFloat("Rotation", &ref.Rotation, 0.5f);
            ImGui::SetNextItemWidth(fullW);
            ImGui::DragFloat2("Scale##RT", glm::value_ptr(ref.Scale), 0.01f, 0.01f, 100.0f);
            ImGui::TreePop();
        }
        if (removeComponent) {
            for (auto e : m_SelectedEntities) e.RemoveComponent<RectTransformComponent>();
        }
    }

    void PropertiesPanel::DrawUIImageComponent(Entity referenceEntity, float uiScale) {
        bool allHave = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<UIImageComponent>()) { allHave = false; break; }
        if (!allHave) return;

        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.8f, 1.0f));
        bool opened = ImGui::TreeNodeEx((void*)typeid(UIImageComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_IMAGE " UI Image");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        bool removeComponent = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) removeComponent = true;
            ImGui::EndPopup();
        }

        if (opened) {
            auto& ref = referenceEntity.GetComponent<UIImageComponent>();
            auto getName = [&]() -> std::string {
                return m_SelectedEntities.size() == 1 ? referenceEntity.GetComponent<TagComponent>().Tag : std::to_string(m_SelectedEntities.size()) + " entities";
            };

            ImGui::ColorEdit4("Color", glm::value_ptr(ref.Color));

            ImGui::Spacing();
            ImGui::Text("Texture");

            ImVec2 textureSlotSize = { 64.0f * uiScale, 64.0f * uiScale };

            if (ref.TextureHandle != 0 && AssetManager::IsAssetHandleValid(ref.TextureHandle)) {
                auto tex = AssetManager::GetAsset<Texture2D>(ref.TextureHandle);
                if (tex && tex->GetImGuiTextureID() != nullptr) {
                    bool vk = (RendererAPI::GetAPI() == RendererAPI::API::Vulkan);
                    ImVec2 uv0 = vk ? ImVec2(0, 0) : ImVec2(0, 1);
                    ImVec2 uv1 = vk ? ImVec2(1, 1) : ImVec2(1, 0);
                    ImGui::Image((ImTextureID)tex->GetImGuiTextureID(), textureSlotSize, uv0, uv1);
                } else {
                    ImGui::Button("Loading...", textureSlotSize);
                }
            } else {
                ImGui::Button("Empty", textureSlotSize);
            }

            // 拖放贴图
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    UUID droppedHandle = *(const UUID*)payload->Data;
                    auto meta = AssetManager::GetMetadata(droppedHandle);
                    if (droppedHandle != 0 && meta.Type == AssetType::Texture2D) {
                        if (!meta.VirtualPath.empty())
                            AssetManager::ImportAsset(VFS::ResolveString(meta.VirtualPath));
                        std::vector<UIImageComponent> oldComps;
                        for (auto e : m_SelectedEntities) oldComps.push_back(e.GetComponent<UIImageComponent>());
                        for (auto e : m_SelectedEntities)
                            e.GetComponent<UIImageComponent>().TextureHandle = droppedHandle;
                        auto macroCmd = std::make_shared<MacroCommand>("Assign UI Image Texture to " + getName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i)
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<UIImageComponent>>(
                                m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<UIImageComponent>()));
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // 移除贴图
            if (ref.TextureHandle != 0) {
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textureSlotSize.y * 0.5f - 10.0f * uiScale);
                if (ImGui::Button("Remove")) {
                    std::vector<UIImageComponent> oldComps;
                    for (auto e : m_SelectedEntities) oldComps.push_back(e.GetComponent<UIImageComponent>());
                    for (auto e : m_SelectedEntities) e.GetComponent<UIImageComponent>().TextureHandle = 0;
                    auto macroCmd = std::make_shared<MacroCommand>("Remove UI Image Texture from " + getName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i)
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<UIImageComponent>>(
                            m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<UIImageComponent>()));
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                }
            }

            ImGui::TreePop();
        }
        if (removeComponent) {
            for (auto e : m_SelectedEntities) e.RemoveComponent<UIImageComponent>();
        }
    }

    void PropertiesPanel::DrawUITextComponent(Entity referenceEntity) {
        bool allHave = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<UITextComponent>()) { allHave = false; break; }
        if (!allHave) return;

        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.6f, 0.2f, 1.0f));
        bool opened = ImGui::TreeNodeEx((void*)typeid(UITextComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_FONT " UI Text");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        bool removeComponent = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) removeComponent = true;
            ImGui::EndPopup();
        }

        if (opened) {
            auto& ref = referenceEntity.GetComponent<UITextComponent>();
            char buf[256];
            strncpy(buf, ref.Text.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
            if (ImGui::InputText("Text", buf, sizeof(buf)))
                ref.Text = buf;
            ImGui::ColorEdit4("Color", glm::value_ptr(ref.Color));
            ImGui::DragFloat("Font Size", &ref.FontSize, 0.5f, 6.0f, 256.0f);
            ImGui::TreePop();
        }
        if (removeComponent) {
            for (auto e : m_SelectedEntities) e.RemoveComponent<UITextComponent>();
        }
    }

    void PropertiesPanel::DrawUIButtonComponent(Entity referenceEntity) {
        bool allHave = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<UIButtonComponent>()) { allHave = false; break; }
        if (!allHave) return;

        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
        bool opened = ImGui::TreeNodeEx((void*)typeid(UIButtonComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_HAND_POINTER " UI Button");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        bool removeComponent = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) removeComponent = true;
            ImGui::EndPopup();
        }

        if (opened) {
            auto& ref = referenceEntity.GetComponent<UIButtonComponent>();
            const char* states[] = { "Normal", "Hover", "Pressed", "Disabled" };
            int cur = (int)ref.CurrentState;
            ImGui::Combo("State", &cur, states, 4);
            ImGui::ColorEdit4("Normal Color", glm::value_ptr(ref.NormalColor));
            ImGui::ColorEdit4("Hover Color", glm::value_ptr(ref.HoverColor));
            ImGui::ColorEdit4("Pressed Color", glm::value_ptr(ref.PressedColor));
            char buf[128];
            strncpy(buf, ref.OnClickCallback.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
            if (ImGui::InputText("OnClick", buf, sizeof(buf)))
                ref.OnClickCallback = buf;
            ImGui::TreePop();
        }
        if (removeComponent) {
            for (auto e : m_SelectedEntities) e.RemoveComponent<UIButtonComponent>();
        }
    }

    void PropertiesPanel::DrawAddComponentButton(Entity referenceEntity, float uiScale) {
        // ==========================================
        // “添加组件” 按钮 (基于第一个实体判定，给所有实体添加)
        // ==========================================
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 动态计算按钮大小
        float addBtnWidth = 150.0f * uiScale;
        float addBtnHeight = 30.0f * uiScale; 
        
        float minX = ImGui::GetWindowContentRegionMin().x;
        float maxX = ImGui::GetWindowContentRegionMax().x;
        float trueContentWidth = maxX - minX;
        
        ImGui::SetCursorPosX(minX + (trueContentWidth - addBtnWidth) * 0.5f);
        
        if (ImGui::Button("Add Component", ImVec2(addBtnWidth, addBtnHeight))) {
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

            // ==========================================
            // 【核心架构锁】：MeshRenderer 和 SpriteRenderer 互斥
            // ==========================================
            bool hasMesh = referenceEntity.HasComponent<MeshRendererComponent>();
            bool hasSprite = referenceEntity.HasComponent<SpriteRendererComponent>();

            if (!hasMesh && !hasSprite) {
                // 如果两个都没有，则两个都可以选
                if (ImGui::MenuItem("Mesh Renderer")) {
                    for (auto e : m_SelectedEntities) {
                        if (!e.HasComponent<MeshRendererComponent>() && !e.HasComponent<SpriteRendererComponent>()) {
                            auto& mrc = e.AddComponent<MeshRendererComponent>();
                            mrc.ModelHandle = AssetManager::GetBuiltInCube();
                            mrc.MaterialHandle = AssetManager::GetBuiltInMaterial();
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::MenuItem("Sprite Renderer")) {
                    for (auto e : m_SelectedEntities) {
                        if (!e.HasComponent<SpriteRendererComponent>() && !e.HasComponent<MeshRendererComponent>()) {
                            e.AddComponent<SpriteRendererComponent>();
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            } else if (hasMesh) {
                // 如果已经有了 MeshRenderer，菜单里用灰色文本提示互斥
                ImGui::TextDisabled("Sprite Renderer (Conflicts with Mesh)");
            } else if (hasSprite) {
                // 如果已经有了 SpriteRenderer，菜单里用灰色文本提示互斥
                ImGui::TextDisabled("Mesh Renderer (Conflicts with Sprite)");
            }

            if (!referenceEntity.HasComponent<DirectionalLightComponent>()) {
                if (ImGui::MenuItem("Directional Light")) {
                    for (auto e : m_SelectedEntities)
                    { 
                        if (!e.HasComponent<DirectionalLightComponent>()) 
                        {
                            auto& dlc = e.AddComponent<DirectionalLightComponent>();
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!referenceEntity.HasComponent<PointLightComponent>()) {
                if (ImGui::MenuItem("Point Light")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<PointLightComponent>()) e.AddComponent<PointLightComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!referenceEntity.HasComponent<EnvironmentComponent>()) {
                if (ImGui::MenuItem("Environment (Skybox)")) {
                    for (auto e : m_SelectedEntities) {
                        if (!e.HasComponent<EnvironmentComponent>()) {
                            e.AddComponent<EnvironmentComponent>();
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!referenceEntity.HasComponent<PostProcessVolumeComponent>()) {
                if (ImGui::MenuItem("Post Process Volume")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<PostProcessVolumeComponent>()) e.AddComponent<PostProcessVolumeComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            // ==========================================
            // 新增：允许用户从菜单中添加 Lua 脚本组件
            // ==========================================
            if (!referenceEntity.HasComponent<LuaScriptComponent>()) {
                if (ImGui::MenuItem("Lua Script")) {
                    for (auto e : m_SelectedEntities) {
                        if (!e.HasComponent<LuaScriptComponent>()) {
                            e.AddComponent<LuaScriptComponent>();
                        }
                    }
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

            ImGui::Separator();
            ImGui::TextDisabled("UI Components");
            if (!referenceEntity.HasComponent<CanvasComponent>()) {
                if (ImGui::MenuItem("Canvas")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<CanvasComponent>()) e.AddComponent<CanvasComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!referenceEntity.HasComponent<RectTransformComponent>()) {
                if (ImGui::MenuItem("Rect Transform")) {
                    for (auto e : m_SelectedEntities) {
                        if (!e.HasComponent<RectTransformComponent>()) e.AddComponent<RectTransformComponent>();
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!referenceEntity.HasComponent<UIImageComponent>()) {
                if (ImGui::MenuItem("UI Image")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<UIImageComponent>()) e.AddComponent<UIImageComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!referenceEntity.HasComponent<UITextComponent>()) {
                if (ImGui::MenuItem("UI Text")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<UITextComponent>()) e.AddComponent<UITextComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!referenceEntity.HasComponent<UIButtonComponent>()) {
                if (ImGui::MenuItem("UI Button")) {
                    for (auto e : m_SelectedEntities) if (!e.HasComponent<UIButtonComponent>()) e.AddComponent<UIButtonComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
    }

    void PropertiesPanel::DrawAssetInspector() {
        AssetMetadata meta = AssetManager::GetMetadata(m_SelectedAsset);

        ImGui::Text("Asset: %s", meta.VirtualPath.c_str());
        ImGui::Separator();

        if (meta.Type == AssetType::Texture2D) {
            static TextureImportSettings s_EditingSettings;
            static UUID s_EditingHandle = 0;

            if (s_EditingHandle != m_SelectedAsset) {
                s_EditingSettings = meta.TextureSettings;
                s_EditingHandle = m_SelectedAsset;
            }

            float uiScale = ImGui::GetIO().FontGlobalScale;
            float labelW = ImGui::CalcTextSize("Filter Mode").x + 16.0f * uiScale;
            float valueW = std::max(50.0f, ImGui::GetContentRegionAvail().x - labelW - 8.0f * uiScale);

            const char* filterTypes[] = { "Linear (Bilinear)", "Nearest (Point)" };
            int currentFilter = (int)s_EditingSettings.Filter;
            ImGui::Text("Filter Mode"); ImGui::SameLine(labelW);
            ImGui::SetNextItemWidth(valueW);
            if (ImGui::Combo("##FilterMode", &currentFilter, filterTypes, 2))
                s_EditingSettings.Filter = (TextureFilterMode)currentFilter;

            const char* wrapTypes[] = { "Repeat", "Clamp" };
            int currentWrap = (int)s_EditingSettings.Wrap;
            ImGui::Text("Wrap Mode"); ImGui::SameLine(labelW);
            ImGui::SetNextItemWidth(valueW);
            if (ImGui::Combo("##WrapMode", &currentWrap, wrapTypes, 2))
                s_EditingSettings.Wrap = (TextureWrapMode)currentWrap;
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Checkbox("sRGB (Color Texture)", &s_EditingSettings.SRGB);
            ImGui::Checkbox("Generate Mipmaps", &s_EditingSettings.GenerateMipmaps);
            ImGui::Checkbox("Flip Y", &s_EditingSettings.FlipY);

            ImGui::Spacing();
            if (ImGui::Button("Apply", ImVec2(-1, 0))) {
                AssetManager::UpdateMetadataSettings(m_SelectedAsset, s_EditingSettings);
                AssetManager::ReloadAsset(m_SelectedAsset);
            }
        } else if (meta.Type == AssetType::Model) {
            float uiScale = ImGui::GetIO().FontGlobalScale;
            auto model = AssetManager::GetAsset<Model>(m_SelectedAsset);

            // ==========================================
            // Editing state — snapshot settings on first frame
            // ==========================================
            static ModelImportSettings s_EditingModelSettings;
            static UUID s_EditingModelHandle = 0;
            if (s_EditingModelHandle != m_SelectedAsset) {
                s_EditingModelSettings = meta.ModelSettings;
                s_EditingModelHandle = m_SelectedAsset;
            }

            // ==========================================
            // ▼ Mesh Info
            // ==========================================
            if (ImGui::CollapsingHeader("Mesh Info", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (model) {
                    uint32_t meshCount = (uint32_t)model->GetMeshes().size();
                    uint32_t totalVerts = 0, totalTris = 0;
                    glm::vec3 bbMin( FLT_MAX), bbMax(-FLT_MAX);
                    for (auto& m : model->GetMeshes()) {
                        totalVerts += m->GetVertexCount();
                        totalTris  += m->GetIndexCount() / 3;
                        const AABB& box = m->GetAABB();
                        bbMin = glm::min(bbMin, box.Min);
                        bbMax = glm::max(bbMax, box.Max);
                    }
                    glm::vec3 bbSize = bbMax - bbMin;

                    // Dynamic label width from the longest label text
                    float labelW = ImGui::CalcTextSize("Bounding Box").x + 12.0f * uiScale;
                    float maxValX = ImGui::GetWindowContentRegionMax().x - 4.0f * uiScale;

                    auto Row = [&](const char* label, const char* val) {
                        ImGui::Text("%s", label);
                        ImGui::SameLine(labelW);
                        ImGui::PushTextWrapPos(maxValX);
                        ImGui::TextColored(ImVec4(0.7f,0.7f,0.8f,1.0f), "%s", val);
                        ImGui::PopTextWrapPos();
                    };

                    Row("Submeshes",  std::to_string(meshCount).c_str());
                    Row("Vertices",   std::to_string(totalVerts).c_str());
                    Row("Triangles",  std::to_string(totalTris).c_str());

                    ImGui::Spacing();
                    bool bbOpen = ImGui::TreeNodeEx("Bounding Box", ImGuiTreeNodeFlags_DefaultOpen);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.6f,1.0f), "(local)");
                    if (bbOpen) {
                        Row("Min",  fmt::format("{:.2f}, {:.2f}, {:.2f}", bbMin.x, bbMin.y, bbMin.z).c_str());
                        Row("Max",  fmt::format("{:.2f}, {:.2f}, {:.2f}", bbMax.x, bbMax.y, bbMax.z).c_str());
                        Row("Size", fmt::format("{:.2f}, {:.2f}, {:.2f}", bbSize.x, bbSize.y, bbSize.z).c_str());
                        ImGui::TreePop();
                    }

                    if (meshCount > 1) {
                        ImGui::Spacing();
                        bool smOpen = ImGui::TreeNode("Submesh Details");
                        if (smOpen) {
                            float maxX = ImGui::GetWindowContentRegionMax().x - 4.0f * uiScale;
                            ImGui::PushTextWrapPos(maxX);
                            uint32_t idx = 0;
                            for (auto& m : model->GetMeshes()) {
                                ImGui::BulletText("#%u  %u verts  %u tris",
                                    idx, m->GetVertexCount(), m->GetIndexCount() / 3);
                                idx++;
                            }
                            ImGui::PopTextWrapPos();
                            ImGui::TreePop();
                        }
                    }
                } else {
                    ImGui::TextDisabled("Model not loaded");
                }
            }

            // ==========================================
            // ▼ Import Settings
            // ==========================================
            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Import Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                float labelW = ImGui::CalcTextSize("Global Scale").x + 16.0f * uiScale;
                float valueW = std::max(60.0f, ImGui::GetContentRegionAvail().x - labelW - 8.0f * uiScale);

                // Global Scale
                ImGui::Text("Global Scale"); ImGui::SameLine(labelW);
                ImGui::SetNextItemWidth(valueW);
                ImGui::DragFloat("##GlobalScale", &s_EditingModelSettings.GlobalScale, 0.01f, 0.01f, 100.0f, "%.3f");

                // Normals
                ImGui::Text("Normals"); ImGui::SameLine(labelW);
                ImGui::SetNextItemWidth(valueW);
                const char* normalItems[] = { "Import", "Calculate", "None" };
                int curNormal = (int)s_EditingModelSettings.Normals;
                ImGui::Combo("##Normals", &curNormal, normalItems, 3);
                s_EditingModelSettings.Normals = (NormalMode)curNormal;

                // Tangents
                ImGui::Text("Tangents"); ImGui::SameLine(labelW);
                ImGui::SetNextItemWidth(valueW);
                const char* tangentItems[] = { "Import", "Calculate", "None" };
                int curTangent = (int)s_EditingModelSettings.Tangents;
                ImGui::Combo("##Tangents", &curTangent, tangentItems, 3);
                s_EditingModelSettings.Tangents = (TangentMode)curTangent;

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Toggles (full width)
                ImGui::Checkbox("Import Materials",   &s_EditingModelSettings.ImportMaterials);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Import material slots from the source file");
                ImGui::Checkbox("Weld Vertices",      &s_EditingModelSettings.WeldVertices);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Merge vertices within threshold to reduce count");
                ImGui::Checkbox("Mesh Compression",   &s_EditingModelSettings.MeshCompression);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable GPU-friendly vertex cache optimization");
                ImGui::Checkbox("Swap Y/Z",           &s_EditingModelSettings.SwapYZ);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Convert between coordinate systems (e.g. Z-up to Y-up)");

                ImGui::Spacing();
                if (ImGui::Button("Apply", ImVec2(-1, 0))) {
                    AssetManager::UpdateMetadataSettings(m_SelectedAsset, s_EditingModelSettings);
                }
            }

            // ==========================================
            // ▼ Preview
            // ==========================================
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float availWidth = ImGui::GetContentRegionAvail().x;
            ImVec2 previewSize(availWidth, availWidth);

            static glm::vec2 s_PreviewRotation(0.3f, -0.6f);
            static UUID s_LastPreviewAsset = 0;
            if (s_LastPreviewAsset != m_SelectedAsset) {
                s_PreviewRotation = glm::vec2(0.3f, -0.6f);
                s_LastPreviewAsset = m_SelectedAsset;
            }

            auto previewTex = AssetPreviewer::RenderRealtimePreview(m_SelectedAsset, s_PreviewRotation, 256);
            if (previewTex) {
                bool isVulkan = RendererAPI::GetAPI() == RendererAPI::API::Vulkan;
                ImVec2 uv0 = isVulkan ? ImVec2(0, 0) : ImVec2(0, 1);
                ImVec2 uv1 = isVulkan ? ImVec2(1, 1) : ImVec2(1, 0);

                ImGui::Image((ImTextureID)previewTex->GetImGuiTextureID(),
                    previewSize, uv0, uv1);

                // InvisibleButton overlay for robust drag-to-rotate interaction.
                // ImageButton's IsItemActive() can lose tracking when the cursor
                // leaves the item rect, so we separate display from interaction.
                ImVec2 imgPos = ImGui::GetItemRectMin();
                ImGui::SetCursorScreenPos(imgPos);
                ImGui::InvisibleButton("##ModelPreviewDrag", previewSize, ImGuiButtonFlags_MouseButtonLeft);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                    s_PreviewRotation.x += delta.y * 0.01f;
                    s_PreviewRotation.y -= delta.x * 0.01f;
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                }
            } else {
                ImVec2 cursor = ImGui::GetCursorPos();
                ImGui::Dummy(previewSize);
                ImGui::SetCursorPos(cursor);
                ImGui::TextDisabled("Loading...");
            }
        } else {
            ImGui::TextDisabled("No import settings available for this asset type");
        }
    }


}
