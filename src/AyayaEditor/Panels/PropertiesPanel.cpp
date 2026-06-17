#include "ayapch.h"
#include "PropertiesPanel.hpp"
#include "../EditorLayer.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Core/EditorCommands.hpp"
#include "Asset/AssetManager.hpp"
#include "Asset/Prefab.hpp"
#include "Renderer/MaterialSerializer.hpp"
#include "Renderer/AssetPreviewer.hpp"
#include "Renderer/Model.hpp"
#include "Engine/Scripting/ScriptEngine.hpp"
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

        // Alpha / transparency
        MaterialProperty p10; p10.UniformName = "u_Alpha"; p10.DisplayName = "Alpha Multiplier"; p10.Type = MaterialPropertyType::Float; p10.FloatValue = 1.0f; mat->Properties.push_back(p10);
        MaterialProperty p11; p11.UniformName = "u_UseAlphaMap"; p11.DisplayName = "Enable Alpha Map"; p11.Type = MaterialPropertyType::Bool; p11.BoolValue = false; mat->Properties.push_back(p11);
        MaterialProperty p12; p12.UniformName = "u_AlphaMap"; p12.DisplayName = "Alpha/Opacity Map"; p12.Type = MaterialPropertyType::Texture2D; p12.TextureHandle = 0; mat->Properties.push_back(p12);

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
        // Inspector Toolbar
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
            DrawAnimationControllerComponent(referenceEntity);
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
            auto& refTransform = referenceEntity.GetComponent<TransformComponent>();

            auto getTargetName = [&]() -> std::string {
                if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                return std::to_string(m_SelectedEntities.size()) + " Entities";
            };

            static std::vector<TransformComponent> s_OldTransforms;
            std::vector<TransformComponent> tempTransforms;
            for (auto e : m_SelectedEntities) tempTransforms.push_back(e.GetComponent<TransformComponent>());

            bool remove = false;
            if (UI::DrawComponentHeader("Transform", ICON_FA_ARROWS_ALT " ",
                                         ImVec4(1,1,1,1), (void*)"TransformComponent",
                                         true, &remove)) {
                UI::BeginPropertyTable("TransformProps");

                // Freeze old-state snapshot before first drag frame.
                // `s_WasActive` prevents re-capture on the deactivation frame.
                static bool s_WasActive = false;
                if (!ImGui::IsAnyItemActive() && !s_WasActive)
                    s_OldTransforms = tempTransforms;
                s_WasActive = ImGui::IsAnyItemActive();

                auto commitVec3 = [&](const char* action, size_t idx) {
                    auto macroCmd = std::make_shared<MacroCommand>(
                        std::string("Change ") + action + " of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        Entity e = m_SelectedEntities[i];
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<TransformComponent>>(
                            e, s_OldTransforms[i], e.GetComponent<TransformComponent>()));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                };

                // Position
                {
                    bool committed = false;
                    glm::vec3 v = refTransform.Translation;
                    if (UI::DrawVec3Control("Position", v, 0.0f, &committed))
                        for (auto e : m_SelectedEntities) e.GetComponent<TransformComponent>().Translation = v;
                    if (committed) commitVec3("Position", 0);
                }

                // Rotation
                {
                    bool committed = false;
                    glm::vec3 v = glm::degrees(refTransform.Rotation);
                    if (UI::DrawVec3Control("Rotation", v, 0.0f, &committed))
                        for (auto e : m_SelectedEntities) e.GetComponent<TransformComponent>().Rotation = glm::radians(v);
                    if (committed) commitVec3("Rotation", 0);
                }

                // Scale
                {
                    bool committed = false;
                    glm::vec3 v = refTransform.Scale;
                    if (UI::DrawVec3Control("Scale", v, 1.0f, &committed))
                        for (auto e : m_SelectedEntities) e.GetComponent<TransformComponent>().Scale = v;
                    if (committed) commitVec3("Scale", 0);
                }

                ImGui::EndTable();
                ImGui::TreePop();
            }
            if (remove) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<TransformComponent>();
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
            auto& refSrc = referenceEntity.GetComponent<SpriteRendererComponent>();
            bool removeComponent = false;
            if (UI::DrawComponentHeader("Sprite Renderer", ICON_FA_IMAGE " ",
                                         ImVec4(0.8f, 0.3f, 0.8f, 1.0f),
                                         (void*)"SpriteRendererComponent",
                                         true, &removeComponent)) {
                
                // 动态名字生成器
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // ------------------------------------------
                // 1. Color 颜色面板 (带状态拦截)
                // ------------------------------------------
                static std::vector<SpriteRendererComponent> s_OldSprites;

                UI::BeginPropertyTable("SpriteProps");
                UI::DrawPropertyLabel("Color");
                glm::vec4 color = refSrc.Color;
                if (ImGui::ColorEdit4("##Color", glm::value_ptr(color)))
                    for (auto e : m_SelectedEntities) e.GetComponent<SpriteRendererComponent>().Color = color;
                if (ImGui::IsItemActivated()) {
                    s_OldSprites.clear();
                    for (auto e : m_SelectedEntities) s_OldSprites.push_back(e.GetComponent<SpriteRendererComponent>());
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    auto macroCmd = std::make_shared<MacroCommand>("Change Sprite Color of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<SpriteRendererComponent>>(
                            m_SelectedEntities[i], s_OldSprites[i], m_SelectedEntities[i].GetComponent<SpriteRendererComponent>()));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                }

                UI::DrawPropertyLabel("Texture");

                ImVec2 texSlot = { 64.0f * uiScale, 64.0f * uiScale };
                
                if (refSrc.TextureHandle != 0 && AssetManager::IsAssetHandleValid(refSrc.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(refSrc.TextureHandle);
                    if (tex && tex->GetImGuiTextureID() != nullptr) {
                        ImVec2 uv0 = tex->IsDataFlipped() ? ImVec2(0, 1) : ImVec2(0, 0);
                        ImVec2 uv1 = tex->IsDataFlipped() ? ImVec2(1, 0) : ImVec2(1, 1);
                        ImGui::Image((ImTextureID)tex->GetImGuiTextureID(), texSlot, uv0, uv1);
                    } else {
                        ImGui::Button("Loading...", texSlot);
                    }
                } else {
                    ImGui::Button("Empty", texSlot);
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
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + texSlot.y * 0.5f - 10.0f * uiScale);
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

                ImGui::EndTable();
                ImGui::TreePop();
            }

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
            auto& refCamera = referenceEntity.GetComponent<CameraComponent>();
            bool removeComponent = false;
            if (UI::DrawComponentHeader("Camera", ICON_FA_VIDEO " ",
                                         ImVec4(0.2f, 0.6f, 0.9f, 1.0f),
                                         (void*)"CameraComponent",
                                         true, &removeComponent)) {
                
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

                UI::BeginPropertyTable("CameraProps");

                // Projection mode (instant combo)
                UI::DrawPropertyLabel("Projection");
                const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
                const char* currentProjectionTypeString = projectionTypeStrings[(int)refCamera.Camera.GetProjectionType()];
                if (ImGui::BeginCombo("##Projection", currentProjectionTypeString)) {
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
                    UI::DrawPropertyLabel("FOV");
                    float fov = glm::degrees(refCamera.Camera.GetPerspectiveFOV());
                    if (ImGui::DragFloat("##FOV", &fov, 0.1f, 1.0f, 179.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetPerspectiveFOV(glm::radians(fov));
                    }
                    handleDragState("Change FOV"); // 调用复用逻辑

                    UI::DrawPropertyLabel("Near Clip");
                    float nearClip = refCamera.Camera.GetPerspectiveNearClip();
                    if (ImGui::DragFloat("##NearClip", &nearClip, 0.1f, 0.01f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetPerspectiveNearClip(nearClip);
                    }
                    handleDragState("Change Near Clip");

                    UI::DrawPropertyLabel("Far Clip");
                    float farClip = refCamera.Camera.GetPerspectiveFarClip();
                    if (ImGui::DragFloat("##FarClip", &farClip, 1.0f, nearClip + 0.1f, 10000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetPerspectiveFarClip(farClip);
                    }
                    handleDragState("Change Far Clip");

                } else {
                    UI::DrawPropertyLabel("Size");
                    float orthoSize = refCamera.Camera.GetOrthographicSize();
                    if (ImGui::DragFloat("##OrthoSize", &orthoSize, 0.1f, 0.1f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetOrthographicSize(orthoSize);
                    }
                    handleDragState("Change Ortho Size");

                    UI::DrawPropertyLabel("Near Clip");
                    float nearClip = refCamera.Camera.GetOrthographicNearClip();
                    if (ImGui::DragFloat("##NearClip", &nearClip, 0.1f, -1000.0f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetOrthographicNearClip(nearClip);
                    }
                    handleDragState("Change Near Clip");

                    UI::DrawPropertyLabel("Far Clip");
                    float farClip = refCamera.Camera.GetOrthographicFarClip();
                    if (ImGui::DragFloat("##FarClip", &farClip, 0.1f, nearClip + 0.1f, 1000.0f)) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().Camera.SetOrthographicFarClip(farClip);
                    }
                    handleDragState("Change Far Clip");
                }

                // 2. 清除模式 (Combo)
                const char* clearFlagStrings[] = { "Skybox", "Solid Color" };
                const char* currentClearFlagString = clearFlagStrings[(int)refCamera.ClearFlag];
                UI::DrawPropertyLabel("Clear Flags");
                if (ImGui::BeginCombo("##ClearFlags", currentClearFlagString)) {
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
                    UI::DrawPropertyLabel("Background");
                    if (ImGui::ColorEdit4("##Background", glm::value_ptr(bgColor))) {
                        for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().BackgroundColor = bgColor;
                    }
                    handleDragState("Change Background Color");
                }

                ImGui::Separator();

                // 4. 其他基础属性
                bool primary = refCamera.Primary;
                UI::DrawPropertyLabel("Primary Camera");
                if (ImGui::Checkbox("##PrimaryCamera", &primary)) {
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
                UI::DrawPropertyLabel("Fixed Aspect Ratio");
                if (ImGui::Checkbox("##FixedAspect", &fixedAspect)) {
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
                UI::DrawPropertyLabel("EV100 (Exposure)");
                if (ImGui::DragFloat("##EV100", &ev100, 0.1f, -10.0f, 25.0f, "%.2f")) {
                    for (auto e : m_SelectedEntities) e.GetComponent<CameraComponent>().EV100 = ev100;
                }
                handleDragState("Change Exposure");

                ImGui::EndTable();
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
            auto& refDlc = referenceEntity.GetComponent<DirectionalLightComponent>();
            bool removeComponent = false;
            if (UI::DrawComponentHeader("Directional Light", ICON_FA_SUN " ",
                                         ImVec4(0.9f, 0.8f, 0.2f, 1.0f),
                                         (void*)"DirectionalLightComponent",
                                         true, &removeComponent)) {
                
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // ==========================================
                // 【绝妙修复】：在任何 UI 交互发生前，先拍下一张绝对纯净的"快照"！
                // ==========================================
                std::vector<DirectionalLightComponent> pureOldLights;
                for (auto e : m_SelectedEntities) pureOldLights.push_back(e.GetComponent<DirectionalLightComponent>());

                static std::vector<DirectionalLightComponent> s_OldLights;
                auto handleDragState = [&](const std::string& actionName) {
                    if (ImGui::IsItemActivated()) { s_OldLights = pureOldLights; }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        auto macroCmd = std::make_shared<MacroCommand>(actionName + " of " + getTargetName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<DirectionalLightComponent>>(
                                m_SelectedEntities[i], s_OldLights[i], m_SelectedEntities[i].GetComponent<DirectionalLightComponent>()));
                        }
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                };

                UI::BeginPropertyTable("DirLightProps");
                UI::DrawPropertyLabel("Light Color");
                glm::vec3 color = refDlc.Color;
                if (ImGui::ColorEdit3("##LightColor", glm::value_ptr(color)))
                    for (auto e : m_SelectedEntities) e.GetComponent<DirectionalLightComponent>().Color = color;
                handleDragState("Change Light Color");

                UI::DrawPropertyLabel("Illuminance (Lux)");
                float illuminance = refDlc.Illuminance;
                if (ImGui::DragFloat("##Illuminance", &illuminance, 1000.0f, 0.0f, 150000.0f, "%.0f"))
                    for (auto e : m_SelectedEntities) e.GetComponent<DirectionalLightComponent>().Illuminance = illuminance;
                handleDragState("Change Illuminance");
                ImGui::EndTable();

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
            bool removeComponent = false;
            if (UI::DrawComponentHeader("Point Light", ICON_FA_LIGHTBULB " ",
                                         ImVec4(0.9f, 0.6f, 0.1f, 1.0f),
                                         (void*)"PointLightComponent",
                                         true, &removeComponent)) {
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
                
                UI::BeginPropertyTable("PointLightProps");
                UI::DrawPropertyLabel("Color");
                glm::vec3 color = refPlc.Color;
                if (ImGui::ColorEdit3("##Color", glm::value_ptr(color)))
                    for (auto e : m_SelectedEntities) e.GetComponent<PointLightComponent>().Color = color;
                handleDragState("Change Light Color");

                UI::DrawPropertyLabel("Luminous Power (lm)");
                float power = refPlc.LuminousPower;
                if (ImGui::DragFloat("##Power", &power, 50.0f, 0.0f, 100000.0f, "%.0f"))
                    for (auto e : m_SelectedEntities) e.GetComponent<PointLightComponent>().LuminousPower = power;
                handleDragState("Change Luminous Power");

                UI::DrawPropertyLabel("Radius (m)");
                float radius = refPlc.Radius;
                if (ImGui::DragFloat("##Radius", &radius, 0.1f, 0.1f, 1000.0f, "%.1f"))
                    for (auto e : m_SelectedEntities) e.GetComponent<PointLightComponent>().Radius = radius;
                handleDragState("Change PointLight Radius");

                UI::DrawPropertyLabel("Falloff");
                float falloff = refPlc.Falloff;
                if (ImGui::DragFloat("##Falloff", &falloff, 0.05f, 0.0f, 10.0f, "%.2f"))
                    for (auto e : m_SelectedEntities) e.GetComponent<PointLightComponent>().Falloff = falloff;
                handleDragState("Change PointLight Falloff");
                ImGui::EndTable();

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
            bool removeComponent = false;
            if (UI::DrawComponentHeader("Environment (Skybox)", ICON_FA_GLOBE " ",
                                         ImVec4(0.4f, 0.8f, 0.9f, 1.0f),
                                         (void*)"EnvironmentComponent",
                                         true, &removeComponent)) {
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

                UI::BeginPropertyTable("EnvProps");

                UI::DrawPropertyLabel("Type");
                const char* envTypeStrings[] = { "None", "HDR Equirectangular", "LDR Equirectangular", "Classic Cubemap" };
                int currentTypeIdx = (int)refEnv.Type;
                if (ImGui::Combo("##Type", &currentTypeIdx, envTypeStrings, 4)) {
                    std::vector<EnvironmentComponent> oldComps = pureOldEnvs;
                    for (auto& c : oldComps) c.IsDirty = true; // 强行"弄脏"备份数据以触发重绘

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

                UI::DrawPropertyLabel("Intensity");
                float intensity = refEnv.Intensity;
                if (ImGui::DragFloat("##Intensity", &intensity, 100.0f, 0.0f, 150000.0f))
                    for (auto e : m_SelectedEntities) e.GetComponent<EnvironmentComponent>().Intensity = intensity;
                handleDragState("Change Environment Intensity");

                if (refEnv.Type != EnvironmentType::None) {
                    // Equirectangular map slot
                    if (refEnv.Type == EnvironmentType::HDR_Equirectangular || refEnv.Type == EnvironmentType::LDR_Equirectangular) {
                        UI::DrawPropertyLabel("Equirectangular Map");

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
                        UI::DrawPropertyLabel("Cubemap Asset");

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

                UI::DrawPropertyLabel("Ambient Color");
                glm::vec3 ambientColor = refEnv.AmbientColor;
                if (ImGui::ColorEdit3("##AmbientColor", glm::value_ptr(ambientColor), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
                    for (auto e : m_SelectedEntities) e.GetComponent<EnvironmentComponent>().AmbientColor = ambientColor;
                handleDragState("Change Ambient Color");

                ImGui::EndTable();
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
        // (注：材质内部属性的修改属于"资产级别(Asset)"，为保护材质共享(Batching)，暂不纳入组件级撤回栈)
        bool allHaveMeshRenderer = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<MeshRendererComponent>()) { allHaveMeshRenderer = false; break; }

        if (allHaveMeshRenderer) {
            auto& refMrc = referenceEntity.GetComponent<MeshRendererComponent>();
            bool removeComponent = false;
            if (UI::DrawComponentHeader("Mesh Renderer", ICON_FA_CUBE " ",
                                         ImVec4(0.2f, 0.8f, 0.4f, 1.0f),
                                         (void*)"MeshRendererComponent", true, &removeComponent)) {

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
                if (ImGui::TreeNodeEx((void*)"ModelNode", ImGuiTreeNodeFlags_DefaultOpen, "Model")) {
                    ImGui::TextDisabled("Mesh Source");
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
                // 2. Material
                // ==========================================
                ImGui::Spacing();
                if (ImGui::TreeNodeEx((void*)"MaterialNode", ImGuiTreeNodeFlags_DefaultOpen, "Material")) {

                    std::shared_ptr<Material> currentMat = nullptr;
                    if (refMrc.MaterialHandle != 0 && AssetManager::IsAssetHandleValid(refMrc.MaterialHandle)) {
                        currentMat = AssetManager::GetAsset<Material>(refMrc.MaterialHandle);
                    }

                    // Get a nice display name for the material
                    auto getMatDisplayName = [&]() -> std::string {
                        if (!currentMat) return "No Material";
                        if (currentMat->IsBuiltIn())
                            return currentMat->Name.empty() ? "Built-in Default PBR" : currentMat->Name;
                        // Project material: show filename from AssetPath
                        if (!currentMat->AssetPath.empty()) {
                            auto s = currentMat->AssetPath.find_last_of("/\\");
                            return (s != std::string::npos)
                                ? currentMat->AssetPath.substr(s + 1)
                                : currentMat->AssetPath;
                        }
                        return currentMat->Name.empty() ? "Unnamed Material" : currentMat->Name;
                    };

                    bool isBuiltIn = currentMat && currentMat->IsBuiltIn();
                    float btnH = ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f + 4.0f;

                    ImGui::TextDisabled("Material");
                    std::string btnLabel = getMatDisplayName();
                    if (isBuiltIn)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                    ImGui::Button(btnLabel.c_str(), ImVec2(-1.0f, btnH));
                    if (isBuiltIn)
                        ImGui::PopStyleColor();

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

                    // ---- Built-in banner (before action buttons) ----
                    if (isBuiltIn) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
                        ImGui::TextWrapped("Built-in material is read-only. Create a project copy to edit.");
                        ImGui::PopStyleColor();
                    }

                    // ---- Action row: Save/Create + Remove (side by side) ----
                    if (currentMat) {
                        if (isBuiltIn) {
                            // Built-in materials may still have a writable physical path
                            // (e.g., DefaultPBR.mat under assets/Editor/). Try direct save first.
                            std::string physicalPath = AssetManager::GetAssetPhysicalPath(refMrc.MaterialHandle);
                            if (physicalPath.empty() && !currentMat->AssetPath.empty())
                                physicalPath = currentMat->AssetPath;  // fallback: use AssetPath directly
                            bool canSaveDirectly = !physicalPath.empty();

                            if (canSaveDirectly) {
                                if (ImGui::Button("Save to .mat", ImVec2(-1.0f, btnH))) {
                                    MaterialSerializer::Serialize(currentMat, physicalPath);
                                }
                            }
                            if (ImGui::Button("Create Material File", ImVec2(-1.0f, btnH))) {
                                auto newMat = currentMat->Clone();
                                newMat->Name = referenceEntity.GetComponent<TagComponent>().Tag + "_Material";
                                std::string filepath = FileDialogs::SaveFile(
                                    "Ayaya Material (*.mat)\0*.mat\0", newMat->Name + ".mat");
                                if (!filepath.empty()) {
                                    MaterialSerializer::Serialize(newMat, filepath);
                                    UUID newHandle = AssetManager::ImportAsset(filepath);
                                    if (newHandle != 0) {
                                        std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                                        for (auto e : m_SelectedEntities)
                                            e.GetComponent<MeshRendererComponent>().MaterialHandle = newHandle;
                                        commitInstantCommand("Create Material File", oldComps);
                                    }
                                }
                            }
                        } else {
                            if (ImGui::Button("Save to .mat", ImVec2(-1.0f, btnH))) {
                                std::string physicalPath = AssetManager::GetAssetPhysicalPath(refMrc.MaterialHandle);
                                if (!physicalPath.empty())
                                    MaterialSerializer::Serialize(currentMat, physicalPath);
                            }
                        }
                        if (ImGui::Button("Remove Material", ImVec2(-1.0f, btnH))) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().MaterialHandle = 0;
                            commitInstantCommand("Remove Material", oldComps);
                        }
                    } else {
                        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.6f, 1.0f), "No Material Assigned");
                        ImGui::PopFont();
                        if (ImGui::Button("Add Default Material", ImVec2(-1.0f, btnH))) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities)
                                e.GetComponent<MeshRendererComponent>().MaterialHandle = AssetManager::GetBuiltInMaterial();
                            commitInstantCommand("Add Default Material", oldComps);
                        }
                    }

                    // ---- Inline editor (grayed out for built-in) ----
                    if (currentMat) {
                        if (isBuiltIn) ImGui::BeginDisabled(true);

                        ImGui::TextDisabled("Shader: %s", currentMat->ShaderName.c_str());

                        // Blend Mode — table row for consistent height
                        static const char* kBlendNames[] = { "Opaque", "Masked", "Translucent" };
                        int currentBlend = 0;
                        switch (currentMat->GetBlendMode()) {
                            case MaterialBlendMode::Opaque:      currentBlend = 0; break;
                            case MaterialBlendMode::Masked:      currentBlend = 1; break;
                            case MaterialBlendMode::Translucent: currentBlend = 2; break;
                        }
                        ImGui::TextDisabled("Blend Mode");
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::Combo("##BlendMode", &currentBlend, kBlendNames, 3)) {
                            MaterialBlendMode newBlend = MaterialBlendMode::Opaque;
                            switch (currentBlend) {
                                case 0: newBlend = MaterialBlendMode::Opaque; break;
                                case 1: newBlend = MaterialBlendMode::Masked; break;
                                case 2: newBlend = MaterialBlendMode::Translucent; break;
                            }
                            currentMat->SetBlendMode(newBlend);
                            for (auto e : m_SelectedEntities) {
                                auto& mrc = e.GetComponent<MeshRendererComponent>();
                                if (mrc.MaterialHandle != 0) {
                                    auto mat = AssetManager::GetAsset<Material>(mrc.MaterialHandle);
                                    if (mat) mat->SetBlendMode(newBlend);
                                }
                            }
                        }
                        ImGui::Separator();

                        // Ordered categories: Albedo → Normal → Roughness+Metallic → AO → ORM → Other
                        using CatEntry = std::pair<std::string, std::vector<MaterialProperty*>>;
                        std::vector<CatEntry> orderedCats = {
                            {"Albedo", {}}, {"Normal", {}}, {"Metallic", {}},
                            {"Roughness", {}}, {"AO", {}}, {"ORM", {}}, {"Other", {}}
                        };
                        auto& groups = orderedCats;
                        bool hasAlphaTex = false;

                        for (auto& prop : currentMat->Properties) {
                            if (currentMat->GetBlendMode() == MaterialBlendMode::Opaque) {
                                if (prop.UniformName == "u_Alpha" ||
                                    prop.UniformName == "u_UseAlphaMap" ||
                                    prop.UniformName == "u_AlphaMap") continue;
                            }
                            if (currentMat->GetBlendMode() == MaterialBlendMode::Masked) {
                                if (prop.UniformName == "u_UseAlphaMap") continue;
                            }
                            if (prop.UniformName == "u_AlphaMap" && prop.TextureHandle != 0)
                                hasAlphaTex = true;

                            std::string name = prop.UniformName;
                            if (name.find("Albedo") != std::string::npos)
                                groups[0].second.push_back(&prop);
                            else if (name.find("Normal") != std::string::npos)
                                groups[1].second.push_back(&prop);
                            else if (name.find("Metallic") != std::string::npos)
                                groups[2].second.push_back(&prop);
                            else if (name.find("Roughness") != std::string::npos)
                                groups[3].second.push_back(&prop);
                            else if (name.find("AO") != std::string::npos ||
                                     name.find("Ambient") != std::string::npos)
                                groups[4].second.push_back(&prop);
                            else if (name.find("ORM") != std::string::npos)
                                groups[5].second.push_back(&prop);
                            else
                                groups[6].second.push_back(&prop);
                        }

                        for (auto& [catName, props] : groups) {
                            if (props.empty()) continue;
                            ImGui::Spacing();
                            ImGui::SeparatorText(catName.c_str());
                            ImGui::PushID(catName.c_str());
                            if (UI::BeginPropertyTable("CategoryTable", 100.0f, 0.8f)) {
                                for (auto* prop : props) {
                                    ImGui::PushID(prop->UniformName.c_str());

                                    // Texture2D: taller row for 64px thumbnail
                                    if (prop->Type == MaterialPropertyType::Texture2D) {
                                        float slot = 64.0f * uiScale;
                                        ImGui::TableNextRow(ImGuiTableRowFlags_None, slot);
                                        ImGui::TableSetColumnIndex(0);
                                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (slot - ImGui::GetTextLineHeight()) * 0.5f);
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f,0.6f,0.6f,1.0f));
                                        ImGui::TextUnformatted(prop->DisplayName.c_str());
                                        ImGui::PopStyleColor();
                                        ImGui::TableSetColumnIndex(1);

                                        std::shared_ptr<Texture2D> tex = nullptr;
                                        if (prop->TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop->TextureHandle))
                                            tex = AssetManager::GetAsset<Texture2D>(prop->TextureHandle);

                                        if (tex && tex->GetImGuiTextureID() != nullptr && (uint64_t)tex->GetImGuiTextureID() != 0) {
                                            bool vk = (RendererAPI::GetAPI() == RendererAPI::API::Vulkan);
                                            ImGui::Image((ImTextureID)tex->GetImGuiTextureID(), ImVec2(slot,slot), vk?ImVec2(0,0):ImVec2(0,1), vk?ImVec2(1,1):ImVec2(1,0));
                                        } else {
                                            ImGui::Button(tex ? "Loading..." : "Null", ImVec2(slot,slot));
                                        }
                                        if (ImGui::BeginDragDropTarget()) {
                                            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                                UUID h = *(const UUID*)p->Data;
                                                if (h != 0 && AssetManager::GetMetadata(h).Type == AssetType::Texture2D) {
                                                    if (!AssetManager::GetMetadata(h).VirtualPath.empty())
                                                        AssetManager::ImportAsset(VFS::ResolveString(AssetManager::GetMetadata(h).VirtualPath));
                                                    if (prop->TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop->TextureHandle))
                                                        m_TextureGarbageBin.push_back(AssetManager::GetAsset<Texture2D>(prop->TextureHandle));
                                                    prop->TextureHandle = h;
                                                }
                                            }
                                            ImGui::EndDragDropTarget();
                                        }
                                        if (prop->TextureHandle != 0) {
                                            ImGui::SameLine();
                                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + slot * 0.5f - 12.0f);
                                            if (ImGui::Button("X##Remove")) {
                                                if (AssetManager::IsAssetHandleValid(prop->TextureHandle))
                                                    m_TextureGarbageBin.push_back(AssetManager::GetAsset<Texture2D>(prop->TextureHandle));
                                                prop->TextureHandle = 0;
                                            }
                                        }
                                    } else {
                                        // Standard row: label + widget
                                        UI::DrawPropertyLabel(prop->DisplayName.c_str());
                                        switch (prop->Type) {
                                            case MaterialPropertyType::Float: ImGui::SliderFloat("##v", &prop->FloatValue, 0.0f, 1.0f); break;
                                            case MaterialPropertyType::Int:   ImGui::InputInt("##v", &prop->IntValue); break;
                                            case MaterialPropertyType::Bool:  ImGui::Checkbox("##v", &prop->BoolValue); break;
                                            case MaterialPropertyType::Vec2:  ImGui::DragFloat2("##v", glm::value_ptr(prop->Vec2Value), 0.05f); break;
                                            case MaterialPropertyType::Vec3:  ImGui::ColorEdit3("##v", glm::value_ptr(prop->Vec3Value), ImGuiColorEditFlags_NoInputs); break;
                                            case MaterialPropertyType::Vec4:  ImGui::ColorEdit4("##v", glm::value_ptr(prop->Vec4Value), ImGuiColorEditFlags_NoInputs); break;
                                            default: break;
                                        }
                                    }
                                    ImGui::PopID();
                                }
                                ImGui::EndTable();
                            }
                            ImGui::PopID();
                        }

                        // Alpha Cutoff (Masked mode)
                        if (currentMat->GetBlendMode() == MaterialBlendMode::Masked && hasAlphaTex) {
                            ImGui::Spacing();
                            ImGui::SeparatorText("Alpha");
                            if (UI::BeginPropertyTable("AlphaTable", 100.0f, 0.8f)) {
                                UI::DrawPropertyLabel("Cutoff");
                                float cutoff = currentMat->GetAlphaCutoff();
                                if (ImGui::SliderFloat("##AlphaCutoff", &cutoff, 0.0f, 1.0f, "%.3f")) {
                                    currentMat->SetAlphaCutoff(cutoff);
                                    for (auto e : m_SelectedEntities) {
                                        auto& m = e.GetComponent<MeshRendererComponent>();
                                        if (m.MaterialHandle != 0) {
                                            auto mat = AssetManager::GetAsset<Material>(m.MaterialHandle);
                                            if (mat) mat->SetAlphaCutoff(cutoff);
                                        }
                                    }
                                }
                                ImGui::EndTable();
                            }
                        }

                        if (isBuiltIn)
                            ImGui::EndDisabled();
                    }
                    ImGui::Spacing();
                    UI::BeginPropertyTable("RendererSettingsTable", 100.0f, 0.8f);

                    UI::DrawPropertyLabel("Cast Shadows");
                    bool castShadows = refMrc.CastShadows;
                    if (ImGui::Checkbox("##CastShadows", &castShadows)) {
                        std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                        for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().CastShadows = castShadows;
                        commitInstantCommand("Toggle Cast Shadows", oldComps);
                    }

                    UI::DrawPropertyLabel("Receive Shadows");
                    bool receiveShadows = refMrc.ReceiveShadows;
                    if (ImGui::Checkbox("##ReceiveShadows", &receiveShadows)) {
                        std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                        for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().ReceiveShadows = receiveShadows;
                        commitInstantCommand("Toggle Receive Shadows", oldComps);
                    }

                    ImGui::EndTable();
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
            bool removeComponent = false;
            if (UI::DrawComponentHeader("Post Process Volume", ICON_FA_MAGIC " ",
                                         ImVec4(0.7f, 0.4f, 0.9f, 1.0f),
                                         (void*)"PostProcessVolumeComponent", true, &removeComponent)) {
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

                UI::BeginPropertyTable("PPVProps", 100.0f, 0.8f);
                UI::DrawPropertyLabel("Is Global");
                bool isGlobal = refPPV.IsGlobal;
                if (ImGui::Checkbox("##IsGlobal", &isGlobal)) {
                    std::vector<PostProcessVolumeComponent> oldComps = pureOldPPVs;
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().IsGlobal = isGlobal;
                    commitInstantCommand("Toggle Is Global", oldComps);
                }
                if (!refPPV.IsGlobal)
                    ImGui::TextDisabled("Local volumes (Bounding Box Blending) coming soon...");
                ImGui::EndTable();

                ImGui::Spacing();
                ImGui::SeparatorText("Tone Mapping & Exposure");

                UI::BeginPropertyTable("PPV_ToneMap", 100.0f, 0.8f);
                UI::DrawPropertyLabel("Algorithm");
                const char* tmTypes[] = { "None", "ACES (Filmic)", "Reinhard" };
                int currentTmType = refPPV.ToneMappingType;
                if (ImGui::Combo("##Algorithm", &currentTmType, tmTypes, 3)) {
                    std::vector<PostProcessVolumeComponent> oldComps = pureOldPPVs;
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().ToneMappingType = currentTmType;
                    commitInstantCommand("Change Tone Mapping Type", oldComps);
                }
                UI::DrawPropertyLabel("Exposure Comp.");
                float exposure = refPPV.Exposure;
                if (ImGui::DragFloat("##Exposure", &exposure, 0.05f, 0.0f, 10.0f, "%.2f"))
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().Exposure = exposure;
                handleDragState("Change Exposure");
                ImGui::EndTable();

                ImGui::Spacing();
                ImGui::SeparatorText("Bloom");

                UI::BeginPropertyTable("PPV_Bloom", 100.0f, 0.8f);
                UI::DrawPropertyLabel("Enable Bloom");
                bool enableBloom = refPPV.EnableBloom;
                if (ImGui::Checkbox("##EnableBloom", &enableBloom)) {
                    std::vector<PostProcessVolumeComponent> oldComps = pureOldPPVs;
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().EnableBloom = enableBloom;
                    commitInstantCommand("Toggle Enable Bloom", oldComps);
                }
                if (refPPV.EnableBloom) {
                    UI::DrawPropertyLabel("  Threshold");
                    float threshold = refPPV.BloomThreshold;
                    if (ImGui::DragFloat("##BloomThresh", &threshold, 0.05f, 0.0f, 10.0f, "%.2f"))
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().BloomThreshold = threshold;
                    handleDragState("Change Bloom Threshold");

                    UI::DrawPropertyLabel("  Soft Knee");
                    float knee = refPPV.BloomKnee;
                    if (ImGui::DragFloat("##BloomKnee", &knee, 0.01f, 0.0001f, 1.0f, "%.2f"))
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().BloomKnee = knee;
                    handleDragState("Change Bloom Knee");

                    UI::DrawPropertyLabel("  Filter Radius");
                    float radius = refPPV.BloomRadius;
                    if (ImGui::DragFloat("##BloomRadius", &radius, 0.0005f, 0.001f, 0.02f, "%.4f"))
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().BloomRadius = radius;
                    handleDragState("Change Bloom Radius");

                    UI::DrawPropertyLabel("  Intensity");
                    float intensity = refPPV.BloomIntensity;
                    if (ImGui::DragFloat("##BloomIntensity", &intensity, 0.05f, 0.0f, 5.0f, "%.2f"))
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().BloomIntensity = intensity;
                    handleDragState("Change Bloom Intensity");
                }
                ImGui::EndTable();

                ImGui::Spacing();
                ImGui::SeparatorText("Anti-Aliasing");

                UI::BeginPropertyTable("PPV_AA", 100.0f, 0.8f);
                UI::DrawPropertyLabel("Enable FXAA");
                bool enableFXAA = refPPV.EnableFXAA;
                if (ImGui::Checkbox("##EnableFXAA", &enableFXAA)) {
                    std::vector<PostProcessVolumeComponent> oldComps = pureOldPPVs;
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().EnableFXAA = enableFXAA;
                    commitInstantCommand("Toggle Enable FXAA", oldComps);
                }
                ImGui::EndTable();

                ImGui::Spacing();
                ImGui::SeparatorText("SSAO (Screen-Space Ambient Occlusion)");

                UI::BeginPropertyTable("PPV_SSAO", 100.0f, 0.8f);
                UI::DrawPropertyLabel("Enable SSAO");
                bool enableSSAO = refPPV.EnableSSAO;
                if (ImGui::Checkbox("##EnableSSAO", &enableSSAO)) {
                    std::vector<PostProcessVolumeComponent> oldComps = pureOldPPVs;
                    for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().EnableSSAO = enableSSAO;
                    commitInstantCommand("Toggle Enable SSAO", oldComps);
                }
                if (enableSSAO) {
                    UI::DrawPropertyLabel("  Radius");
                    float ssaoRadius = refPPV.SSAORadius;
                    if (ImGui::DragFloat("##SSAORadius", &ssaoRadius, 0.01f, 0.1f, 3.0f, "%.2f"))
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().SSAORadius = ssaoRadius;
                    handleDragState("Change SSAO Radius");

                    UI::DrawPropertyLabel("  Bias");
                    float ssaoBias = refPPV.SSAOBias;
                    if (ImGui::DragFloat("##SSAOBias", &ssaoBias, 0.001f, 0.001f, 0.2f, "%.3f"))
                        for (auto e : m_SelectedEntities) e.GetComponent<PostProcessVolumeComponent>().SSAOBias = ssaoBias;
                    handleDragState("Change SSAO Bias");
                }
                ImGui::EndTable();

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
            bool removeComponent = false;
            if (UI::DrawComponentHeader("Lua Script", ICON_FA_FILE_CODE " ",
                                         ImVec4(0.9f, 0.2f, 0.5f, 1.0f),
                                         (void*)"LuaScriptComponent", true, &removeComponent)) {
                auto& refLsc = referenceEntity.GetComponent<LuaScriptComponent>();

                // 动态名字生成器
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // 【物理快照】：捕获绝对纯净的旧状态
                std::vector<LuaScriptComponent> pureOldLscs;
                for (auto e : m_SelectedEntities) pureOldLscs.push_back(e.GetComponent<LuaScriptComponent>());

                auto getScriptDisplayName = [&](UUID handle) -> std::string {
                    auto meta = AssetManager::GetMetadata(handle);
                    if (!meta.VirtualPath.empty()) {
                        auto slash = meta.VirtualPath.find_last_of("/\\");
                        return (slash != std::string::npos)
                            ? meta.VirtualPath.substr(slash + 1)
                            : meta.VirtualPath;
                    }
                    return "Script (ID: " + std::to_string((uint64_t)handle) + ")";
                };

                ImGui::TextDisabled("Script Source");
                std::string pathDisplay = "Drop .lua file here";
                if (refLsc.ScriptHandle != 0)
                    pathDisplay = ICON_FA_FILE_CODE " " + getScriptDisplayName(refLsc.ScriptHandle);
                float btnH = ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f + 4.0f;
                ImGui::Button(pathDisplay.c_str(), ImVec2(-1.0f, btnH));

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        UUID droppedHandle = *(const UUID*)payload->Data;
                        if (droppedHandle != 0 && AssetManager::GetMetadata(droppedHandle).Type == AssetType::LuaScript) {
                            std::vector<LuaScriptComponent> oldComps = pureOldLscs;
                            for (auto e : m_SelectedEntities) {
                                ScriptEngine::ReleaseScriptEnv(e);
                                e.GetComponent<LuaScriptComponent>().ScriptHandle = droppedHandle;
                                ScriptEngine::InitEditorScript(e, m_Context.get());
                                ScriptEngine::TriggerRebuild(e, m_Context.get());
                            }
                            auto macroCmd = std::make_shared<MacroCommand>("Assign Lua Script to " + getTargetName());
                            for (size_t i = 0; i < m_SelectedEntities.size(); ++i)
                                macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<LuaScriptComponent>>(
                                    m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<LuaScriptComponent>()));
                            EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::Spacing();

                if (refLsc.ScriptHandle != 0) {
                    if (ImGui::Button(ICON_FA_TRASH " Remove Script", ImVec2(-1.0f, btnH))) {
                        std::vector<LuaScriptComponent> oldComps = pureOldLscs;
                        for (auto e : m_SelectedEntities) {
                            if (e.HasComponent<RelationshipComponent>()) {
                                auto& rel = e.GetComponent<RelationshipComponent>();
                                auto children = rel.Children;
                                for (auto childID : children)
                                    if (m_Context->Reg().valid(childID))
                                        m_Context->DestroyEntity(Entity{childID, m_Context.get()});
                                rel.Children.clear();
                            }
                        }
                        for (auto e : m_SelectedEntities) {
                            ScriptEngine::ReleaseScriptEnv(e);
                            e.GetComponent<LuaScriptComponent>().ScriptHandle = 0;
                        }
                        auto macroCmd = std::make_shared<MacroCommand>("Remove Lua Script from " + getTargetName());
                        for (size_t i = 0; i < m_SelectedEntities.size(); ++i)
                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<LuaScriptComponent>>(
                                m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<LuaScriptComponent>()));
                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                    }
                }

                // Config parameters — property table layout
                if (refLsc.ScriptHandle != 0) {
                    ScriptEngine::InitEditorScript(referenceEntity, m_Context.get());
                    const auto& params = ScriptEngine::GetScriptParams(referenceEntity);
                    if (!params.empty()) {
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::TextDisabled("Script Parameters");

                        UI::BeginPropertyTable("LuaScriptParams");
                        for (const auto& p : params) {
                            ImGui::PushID(p.Name.c_str());

                            if (p.Type == "int") {
                                int v = ScriptEngine::GetConfigInt(referenceEntity, p.Name, 0);
                                UI::DrawPropertyLabel(p.Label.c_str());
                                if (ImGui::DragInt(("##" + p.Name).c_str(), &v, 1.0f, (int)p.Min, (int)p.Max))
                                    ScriptEngine::SetConfigInt(referenceEntity, p.Name, v);
                            }
                            else if (p.Type == "float") {
                                float v = ScriptEngine::GetConfigFloat(referenceEntity, p.Name, 0.0f);
                                UI::DrawPropertyLabel(p.Label.c_str());
                                if (ImGui::DragFloat(("##" + p.Name).c_str(), &v, 0.1f, p.Min, p.Max, "%.2f"))
                                    ScriptEngine::SetConfigFloat(referenceEntity, p.Name, v);
                            }
                            else if (p.Type == "combo") {
                                int v = ScriptEngine::GetConfigInt(referenceEntity, p.Name, 0);
                                UI::DrawPropertyLabel(p.Label.c_str());
                                std::vector<const char*> items;
                                for (auto& opt : p.ComboOpts) items.push_back(opt.c_str());
                                if (ImGui::Combo(("##" + p.Name).c_str(), &v, items.data(), (int)items.size()))
                                    ScriptEngine::SetConfigInt(referenceEntity, p.Name, v);
                            }
                            else if (p.Type == "file") {
                                std::string handleStr = ScriptEngine::GetConfigStr(referenceEntity, p.Name, "0");
                                uint64_t handle = 0;
                                try { handle = std::stoull(handleStr); } catch (...) {}
                                std::string btnLabel = "Drop " + p.FileExt + " here";
                                if (handle != 0) {
                                    auto meta = AssetManager::GetMetadata(UUID(handle));
                                    btnLabel = meta.VirtualPath.empty()
                                        ? ("Asset: " + std::to_string(handle)) : meta.VirtualPath;
                                    auto slash = btnLabel.find_last_of("/\\");
                                    if (slash != std::string::npos) btnLabel = btnLabel.substr(slash + 1);
                                }
                                UI::DrawPropertyLabel(p.Label.c_str());
                                ImGui::Button(btnLabel.c_str(), ImVec2(-FLT_MIN, 0));
                                if (ImGui::BeginDragDropTarget()) {
                                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                        UUID dropped = *(const UUID*)pl->Data;
                                        if (dropped != 0)
                                            ScriptEngine::SetConfigStr(referenceEntity, p.Name, std::to_string((uint64_t)dropped));
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                            }
                            else if (p.Type == "bool") {
                                int v = ScriptEngine::GetConfigInt(referenceEntity, p.Name, 0);
                                bool bv = (v != 0);
                                UI::DrawPropertyLabel(p.Label.c_str());
                                if (ImGui::Checkbox(("##" + p.Name).c_str(), &bv))
                                    ScriptEngine::SetConfigInt(referenceEntity, p.Name, bv ? 1 : 0);
                            }

                            ImGui::PopID();
                        }
                        ImGui::EndTable();
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
            auto& refRb2d = referenceEntity.GetComponent<Rigidbody2DComponent>();
            bool removeComponent = false;
            if (UI::DrawComponentHeader("Rigidbody 2D", ICON_FA_BULLSEYE " ",
                                         ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
                                         (void*)"Rigidbody2DComponent", true, &removeComponent)) {

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

                UI::BeginPropertyTable("Rb2dProps");
                UI::DrawPropertyLabel("Body Type");
                const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
                const char* currentBodyTypeString = bodyTypeStrings[(int)refRb2d.Type];
                if (ImGui::BeginCombo("##BodyType", currentBodyTypeString)) {
                    for (int i = 0; i < 3; i++) {
                        bool isSelected = currentBodyTypeString == bodyTypeStrings[i];
                        if (ImGui::Selectable(bodyTypeStrings[i], isSelected)) {
                            std::vector<Rigidbody2DComponent> oldComps = pureOldRb2ds;
                            for (auto e : m_SelectedEntities)
                                e.GetComponent<Rigidbody2DComponent>().Type = (Rigidbody2DComponent::BodyType)i;
                            commitInstantCommand("Change Body Type", oldComps);
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                UI::DrawPropertyLabel("Fixed Rotation");
                bool fixedRotation = refRb2d.FixedRotation;
                if (ImGui::Checkbox("##FixedRotation", &fixedRotation)) {
                    std::vector<Rigidbody2DComponent> oldComps = pureOldRb2ds;
                    for (auto e : m_SelectedEntities) e.GetComponent<Rigidbody2DComponent>().FixedRotation = fixedRotation;
                    commitInstantCommand("Toggle Fixed Rotation", oldComps);
                }
                ImGui::EndTable();

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
            bool removeComponent = false;
            if (UI::DrawComponentHeader("Box Collider 2D", ICON_FA_VECTOR_SQUARE " ",
                                         ImVec4(0.5f, 0.9f, 0.3f, 1.0f),
                                         (void*)"BoxCollider2DComponent", true, &removeComponent)) {
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

                UI::BeginPropertyTable("Bc2dProps");
                glm::vec2 offset = refBc2d.Offset;
                UI::DrawPropertyLabel("Offset");
                if (ImGui::DragFloat2("##Offset", glm::value_ptr(offset), 0.05f)) {
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Offset = offset;
                }
                handleDragState("Change BoxCollider2D Offset");

                glm::vec2 size = refBc2d.Size;
                UI::DrawPropertyLabel("Size");
                if (ImGui::DragFloat2("##Size", glm::value_ptr(size), 0.05f))
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Size = size;
                handleDragState("Change BoxCollider2D Size");

                float density = refBc2d.Density;
                UI::DrawPropertyLabel("Density");
                if (ImGui::DragFloat("##Density", &density, 0.01f, 0.0f, 10.0f))
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Density = density;
                handleDragState("Change BoxCollider2D Density");

                float friction = refBc2d.Friction;
                UI::DrawPropertyLabel("Friction");
                if (ImGui::DragFloat("##Friction", &friction, 0.01f, 0.0f, 1.0f))
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Friction = friction;
                handleDragState("Change BoxCollider2D Friction");

                float restitution = refBc2d.Restitution;
                UI::DrawPropertyLabel("Restitution");
                if (ImGui::DragFloat("##Restitution", &restitution, 0.01f, 0.0f, 1.0f))
                    for (auto e : m_SelectedEntities) e.GetComponent<BoxCollider2DComponent>().Restitution = restitution;
                handleDragState("Change BoxCollider2D Restitution");

                ImGui::EndTable();
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

        bool removeComponent = false;
        if (UI::DrawComponentHeader("Canvas", ICON_FA_DESKTOP " ",
                                     ImVec4(0.2f, 0.75f, 0.75f, 1.0f),
                                     (void*)"CanvasComponent", true, &removeComponent)) {
            auto& ref = referenceEntity.GetComponent<CanvasComponent>();
            UI::BeginPropertyTable("CanvasProps");
            UI::DrawPropertyLabel("Render Mode");
            const char* modes[] = { "Screen Overlay", "Screen Camera", "World Space" };
            int cur = (int)ref.Mode;
            if (ImGui::Combo("##RenderMode", &cur, modes, 3))
                ref.Mode = (CanvasComponent::RenderMode)cur;
            UI::DrawPropertyLabel("Sort Order");
            ImGui::DragInt("##SortOrder", &ref.SortOrder, 0.1f, -100, 100);
            ImGui::EndTable();
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

        bool removeComponent = false;
        if (UI::DrawComponentHeader("Rect Transform", ICON_FA_OBJECT_GROUP " ",
                                     ImVec4(0.5f, 0.7f, 0.9f, 1.0f),
                                     (void*)"RectTransformComponent", true, &removeComponent)) {
            auto& ref = referenceEntity.GetComponent<RectTransformComponent>();
            UI::BeginPropertyTable("RectTransformProps");

            UI::DrawPropertyLabel("Anchor Min");
            ImGui::DragFloat2("##AnchorMin", glm::value_ptr(ref.AnchorMin), 0.01f, 0.0f, 1.0f);
            UI::DrawPropertyLabel("Anchor Max");
            ImGui::DragFloat2("##AnchorMax", glm::value_ptr(ref.AnchorMax), 0.01f, 0.0f, 1.0f);
            UI::DrawPropertyLabel("Pivot");
            ImGui::DragFloat2("##Pivot", glm::value_ptr(ref.Pivot), 0.01f, 0.0f, 1.0f);
            UI::DrawPropertyLabel("Position");
            ImGui::DragFloat2("##Position", glm::value_ptr(ref.Position), 0.5f);
            UI::DrawPropertyLabel("Size");
            ImGui::DragFloat2("##SizeRT", glm::value_ptr(ref.Size), 0.5f, 0.0f, 10000.0f);
            UI::DrawPropertyLabel("Rotation");
            ImGui::DragFloat("##Rotation", &ref.Rotation, 0.5f);
            UI::DrawPropertyLabel("Scale");
            ImGui::DragFloat2("##ScaleRT", glm::value_ptr(ref.Scale), 0.01f, 0.01f, 100.0f);
            ImGui::EndTable();
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

        bool removeComponent = false;
        if (UI::DrawComponentHeader("UI Image", ICON_FA_IMAGE " ",
                                     ImVec4(0.8f, 0.3f, 0.8f, 1.0f),
                                     (void*)"UIImageComponent", true, &removeComponent)) {
            auto& ref = referenceEntity.GetComponent<UIImageComponent>();
            UI::BeginPropertyTable("UIImageProps");
            auto getName = [&]() -> std::string {
                return m_SelectedEntities.size() == 1 ? referenceEntity.GetComponent<TagComponent>().Tag : std::to_string(m_SelectedEntities.size()) + " entities";
            };

            UI::DrawPropertyLabel("Color");
            ImGui::ColorEdit4("##Color", glm::value_ptr(ref.Color));

            // Texture — taller row with label on left, image on right
            ImVec2 texSlot = { 64.0f * uiScale, 64.0f * uiScale };
            ImGui::TableNextRow(ImGuiTableRowFlags_None, texSlot.y);
            ImGui::TableSetColumnIndex(0);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (texSlot.y - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f,0.6f,0.6f,1.0f));
            ImGui::TextUnformatted("Texture");
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(1);
            if (ref.TextureHandle != 0 && AssetManager::IsAssetHandleValid(ref.TextureHandle)) {
                auto tex = AssetManager::GetAsset<Texture2D>(ref.TextureHandle);
                if (tex && tex->GetImGuiTextureID() != nullptr) {
                    bool vk = (RendererAPI::GetAPI() == RendererAPI::API::Vulkan);
                    ImGui::Image((ImTextureID)tex->GetImGuiTextureID(), texSlot, vk?ImVec2(0,0):ImVec2(0,1), vk?ImVec2(1,1):ImVec2(1,0));
                } else {
                    ImGui::Button("Loading...", texSlot);
                }
            } else {
                ImGui::Button("Empty", texSlot);
            }

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
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + texSlot.y * 0.5f - 10.0f * uiScale);
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

            ImGui::EndTable();
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

        bool removeComponent = false;
        if (UI::DrawComponentHeader("UI Text", ICON_FA_FONT " ",
                                     ImVec4(0.9f, 0.6f, 0.2f, 1.0f),
                                     (void*)"UITextComponent", true, &removeComponent)) {
            auto& ref = referenceEntity.GetComponent<UITextComponent>();
            UI::BeginPropertyTable("UITextProps");
            UI::DrawPropertyLabel("Text");
            char buf[256];
            strncpy(buf, ref.Text.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
            if (ImGui::InputText("##Text", buf, sizeof(buf)))
                ref.Text = buf;
            UI::DrawPropertyLabel("Font Size");
            ImGui::DragFloat("##FontSize", &ref.FontSize, 0.5f, 6.0f, 256.0f);
            UI::DrawPropertyLabel("Color");
            ImGui::ColorEdit4("##Color", glm::value_ptr(ref.Color));
            ImGui::EndTable();
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

        bool removeComponent = false;
        if (UI::DrawComponentHeader("UI Button", ICON_FA_HAND_POINTER " ",
                                     ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
                                     (void*)"UIButtonComponent", true, &removeComponent)) {
            auto& ref = referenceEntity.GetComponent<UIButtonComponent>();
            UI::BeginPropertyTable("UIButtonProps");
            const char* states[] = { "Normal", "Hover", "Pressed", "Disabled" };
            int cur = (int)ref.CurrentState;
            ImGui::Combo("State", &cur, states, 4);
            UI::DrawPropertyLabel("Normal Color");
            ImGui::ColorEdit4("##NormalColor", glm::value_ptr(ref.NormalColor));
            UI::DrawPropertyLabel("Hover Color");
            ImGui::ColorEdit4("##HoverColor", glm::value_ptr(ref.HoverColor));
            UI::DrawPropertyLabel("Pressed Color");
            ImGui::ColorEdit4("##PressedColor", glm::value_ptr(ref.PressedColor));
            char buf[128];
            strncpy(buf, ref.OnClickCallback.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
            UI::DrawPropertyLabel("OnClick");
            if (ImGui::InputText("##OnClick", buf, sizeof(buf)))
                ref.OnClickCallback = buf;
            ImGui::EndTable();
            ImGui::TreePop();
        }
        if (removeComponent) {
            for (auto e : m_SelectedEntities) e.RemoveComponent<UIButtonComponent>();
        }
    }

    void PropertiesPanel::DrawAnimationControllerComponent(Entity referenceEntity) {
        // Guard: only draw if all selected entities have the component
        bool allHave = true;
        for (auto e : m_SelectedEntities) if (!e.HasComponent<AnimationControllerComponent>()) { allHave = false; break; }
        if (!allHave) return;

        float uiScale = ImGui::GetIO().FontGlobalScale;
        auto& component = referenceEntity.GetComponent<AnimationControllerComponent>();

        if (!UI::DrawComponentHeader("Animation Controller", ICON_FA_FILM " ",
                                      ImVec4(0.3f, 0.9f, 0.8f, 1.0f),
                                      (void*)"AnimationControllerComponent", true))
            return;

        // ---- IsPlaying (works across multi-select) ----
        if (ImGui::Checkbox("Is Playing", &component.IsPlaying))
            for (auto e : m_SelectedEntities) e.GetComponent<AnimationControllerComponent>().IsPlaying = component.IsPlaying;

        // Multi-select safety: track array editing is disabled for multiple entities
        if (m_SelectedEntities.size() > 1) {
            ImGui::Spacing();
            ImGui::TextDisabled("Track editing is not available in multi-select mode.");
            ImGui::TextDisabled("Select a single entity to edit animation tracks.");
            return;
        }

        ImGui::Separator();

        // --- Snapshot for undo ---
        AnimationControllerComponent oldComp = component;
        static AnimationControllerComponent s_OldComp;
        std::string targetName = "'" + referenceEntity.GetComponent<TagComponent>().Tag + "'";

        auto handleDragState = [&](const std::string& actionName) {
            if (ImGui::IsItemActivated()) s_OldComp = oldComp;
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                auto cmd = std::make_shared<ChangeComponentCommand<AnimationControllerComponent>>(
                    referenceEntity, s_OldComp, referenceEntity.GetComponent<AnimationControllerComponent>());
                EditorLayer::Get().GetCommandHistory().AddCommand(cmd);
            }
        };

        // ---- Tracks ----
        int trackToRemove = -1;
        for (int i = 0; i < (int)component.Tracks.size(); i++) {
            auto& track = component.Tracks[i];
            ImGui::PushID(i);

            ImGui::Text("Track %d", i);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 22.0f);
            if (ImGui::SmallButton("X")) {
                trackToRemove = i;
                ImGui::PopID();
                break;
            }

            UI::BeginPropertyTable("AnimTrackProps", 80.0f, 1.8f);

            // Curve Asset
            UI::DrawPropertyLabel("Curve");
            std::string assetLabel = "Drop .curve here";
            if (track.CurveHandle != 0) {
                AssetMetadata meta = AssetManager::GetMetadata(track.CurveHandle);
                const std::string& vpath = meta.VirtualPath;
                if (!vpath.empty()) {
                    auto pos = vpath.find_last_of("/\\");
                    assetLabel = (pos != std::string::npos) ? vpath.substr(pos + 1) : vpath;
                } else {
                    assetLabel = "Curve Assigned";
                }
            }
            ImGui::Button(assetLabel.c_str(), ImVec2(-FLT_MIN, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    UUID h = *(const UUID*)p->Data;
                    if (h != 0 && AssetManager::GetMetadata(h).Type == AssetType::Curve) {
                        track.CurveHandle = h;
                        auto cmd = std::make_shared<ChangeComponentCommand<AnimationControllerComponent>>(
                            referenceEntity, oldComp, referenceEntity.GetComponent<AnimationControllerComponent>());
                        EditorLayer::Get().GetCommandHistory().AddCommand(cmd);
                        oldComp = referenceEntity.GetComponent<AnimationControllerComponent>();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            UI::DrawPropertyLabel("Target");
            int currentProp = static_cast<int>(track.Property);
            if (ImGui::Combo("##Target", &currentProp, s_TargetPropertyStrings, s_TargetPropertyCount)) {
                track.Property = static_cast<TargetProperty>(currentProp);
                auto cmd = std::make_shared<ChangeComponentCommand<AnimationControllerComponent>>(
                    referenceEntity, oldComp, referenceEntity.GetComponent<AnimationControllerComponent>());
                EditorLayer::Get().GetCommandHistory().AddCommand(cmd);
                oldComp = referenceEntity.GetComponent<AnimationControllerComponent>();
            }

            UI::DrawPropertyLabel("Time Offset");
            float timeOff = track.TimeOffset;
            if (ImGui::DragFloat("##TimeOffset", &timeOff, 0.05f, 0.0f, 0.0f, "%.2f s"))
                track.TimeOffset = timeOff;
            handleDragState("Change Track TimeOffset");

            ImGui::EndTable();
            ImGui::Separator();
            ImGui::PopID();
        }

        // Apply deferred removal
        if (trackToRemove >= 0) {
            component.Tracks.erase(component.Tracks.begin() + trackToRemove);
            auto cmd = std::make_shared<ChangeComponentCommand<AnimationControllerComponent>>(
                referenceEntity, oldComp, referenceEntity.GetComponent<AnimationControllerComponent>());
            EditorLayer::Get().GetCommandHistory().AddCommand(cmd);
        }

        // ---- Add Track button ----
        if (ImGui::Button("Add Track", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
            component.Tracks.push_back(AnimationTrack());
            auto cmd = std::make_shared<ChangeComponentCommand<AnimationControllerComponent>>(
                referenceEntity, oldComp, referenceEntity.GetComponent<AnimationControllerComponent>());
            EditorLayer::Get().GetCommandHistory().AddCommand(cmd);
        }

        ImGui::TreePop();
    }

    void PropertiesPanel::DrawAddComponentButton(Entity referenceEntity, float uiScale) {
        // ==========================================
        // "添加组件" 按钮 (基于第一个实体判定，给所有实体添加)
        // ==========================================
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 动态计算按钮大小
        float addBtnWidth = 150.0f * uiScale;
        float addBtnHeight = 30.0f * uiScale;

        // GetContentRegionMax accounts for scrollbar presence dynamically
        float usableW = ImGui::GetContentRegionMax().x
                      - ImGui::GetStyle().WindowPadding.x;
        float btnX = ImGui::GetStyle().WindowPadding.x
                   + (usableW - addBtnWidth) * 0.5f;

        ImGui::SetCursorPosX(btnX);
        
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

            ImGui::Separator();
            ImGui::TextDisabled("Animation");
            if (!referenceEntity.HasComponent<AnimationControllerComponent>()) {
                if (ImGui::MenuItem("Animation Controller")) {
                    for (auto e : m_SelectedEntities)
                        if (!e.HasComponent<AnimationControllerComponent>())
                            e.AddComponent<AnimationControllerComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
    }

    void PropertiesPanel::DrawAssetInspector() {
        AssetMetadata meta = AssetManager::GetMetadata(m_SelectedAsset);

        // Resolve SubMesh → parent Model so mesh info and import settings always display
        UUID inspectHandle = m_SelectedAsset;
        if (meta.Type == AssetType::SubMesh && meta.ParentHandle != 0) {
            inspectHandle = meta.ParentHandle;
            meta = AssetManager::GetMetadata(inspectHandle);
        }

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
            auto model = AssetManager::GetAsset<Model>(inspectHandle);

            // ==========================================
            // Editing state — snapshot settings on first frame
            // ==========================================
            static ModelImportSettings s_EditingModelSettings;
            static UUID s_EditingModelHandle = 0;
            if (s_EditingModelHandle != inspectHandle) {
                s_EditingModelSettings = meta.ModelSettings;
                s_EditingModelHandle = inspectHandle;
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
                    AssetManager::UpdateMetadataSettings(inspectHandle, s_EditingModelSettings);
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
            if (s_LastPreviewAsset != inspectHandle) {
                s_PreviewRotation = glm::vec2(0.3f, -0.6f);
                s_LastPreviewAsset = inspectHandle;
            }

            // Single preview — realtime rendering cost is negligible.
            // Cached thumbnails are for the Content Browser (many icons).
            auto previewTex = AssetPreviewer::RenderRealtimePreview(inspectHandle, s_PreviewRotation, 256);

            if (previewTex) {
                bool isVulkan = RendererAPI::GetAPI() == RendererAPI::API::Vulkan;
                ImVec2 uv0 = isVulkan ? ImVec2(0, 0) : ImVec2(0, 1);
                ImVec2 uv1 = isVulkan ? ImVec2(1, 1) : ImVec2(1, 0);
                ImGui::Image((ImTextureID)previewTex->GetImGuiTextureID(), previewSize, uv0, uv1);

                ImVec2 imgPos = ImGui::GetItemRectMin();
                ImGui::SetCursorScreenPos(imgPos);
                ImGui::InvisibleButton("##ModelPreviewDrag", previewSize, ImGuiButtonFlags_MouseButtonLeft);
                // Reset rotation + drag delta on activation (new click) and on deactivation (release),
                // so each drag session starts from default and snaps back on release.
                if (ImGui::IsItemActivated() || ImGui::IsItemDeactivated()) {
                    s_PreviewRotation = glm::vec2(0.3f, -0.6f);
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                }
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
        } else if (meta.Type == AssetType::Prefab) {
            float uiScale = ImGui::GetIO().FontGlobalScale;
            auto prefab = AssetManager::GetAsset<Prefab>(inspectHandle);
            Scene* prefabScene = prefab ? prefab->GetScene() : nullptr;

            // ==========================================
            // ▼ Prefab Info
            // ==========================================
            if (ImGui::CollapsingHeader("Prefab Info", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (prefabScene) {
                    // Count entities and components
                    uint32_t entityCount = 0;
                    uint32_t meshCount = 0, cameraCount = 0;
                    uint32_t lightCount = 0, spriteCount = 0;
                    uint32_t scriptCount = 0, physicsCount = 0;

                    auto& reg = prefabScene->Reg();

                    // Count root + recursively traverse hierarchy
                    std::function<void(entt::entity)> countRecursive;
                    countRecursive = [&](entt::entity e) {
                        entityCount++;
                        Entity ent{ e, prefabScene };
                        if (ent.HasComponent<MeshRendererComponent>()) meshCount++;
                        if (ent.HasComponent<CameraComponent>()) cameraCount++;
                        if (ent.HasComponent<DirectionalLightComponent>() ||
                            ent.HasComponent<PointLightComponent>()) lightCount++;
                        if (ent.HasComponent<SpriteRendererComponent>()) spriteCount++;
                        if (ent.HasComponent<LuaScriptComponent>()) scriptCount++;
                        if (ent.HasComponent<Rigidbody2DComponent>()) physicsCount++;

                        if (ent.HasComponent<RelationshipComponent>()) {
                            for (auto child : ent.GetComponent<RelationshipComponent>().Children)
                                countRecursive(child);
                        }
                    };

                    for (auto rootHandle : prefabScene->GetRootEntities())
                        countRecursive(rootHandle);

                    float labelW = ImGui::CalcTextSize("Components").x + 16.0f * uiScale;
                    float maxValX = ImGui::GetWindowContentRegionMax().x - 4.0f * uiScale;

                    auto Row = [&](const char* label, const char* val) {
                        ImGui::Text("%s", label);
                        ImGui::SameLine(labelW);
                        ImGui::PushTextWrapPos(maxValX);
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "%s", val);
                        ImGui::PopTextWrapPos();
                    };

                    Row("Entities",   std::to_string(entityCount).c_str());
                    Row("Meshes",     std::to_string(meshCount).c_str());
                    Row("Cameras",    std::to_string(cameraCount).c_str());
                    Row("Lights",     std::to_string(lightCount).c_str());
                    Row("Sprites",    std::to_string(spriteCount).c_str());
                    Row("Scripts",    std::to_string(scriptCount).c_str());
                    Row("Physics 2D", std::to_string(physicsCount).c_str());

                    ImGui::Spacing();

                    // Component breakdown
                    if (ImGui::TreeNodeEx("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
                        std::vector<std::string> compNames;
                        if (meshCount > 0)   compNames.push_back(fmt::format("{} MeshRenderer", meshCount));
                        if (cameraCount > 0) compNames.push_back(fmt::format("{} Camera", cameraCount));
                        if (lightCount > 0)  compNames.push_back(fmt::format("{} Light", lightCount));
                        if (spriteCount > 0) compNames.push_back(fmt::format("{} Sprite", spriteCount));
                        if (scriptCount > 0) compNames.push_back(fmt::format("{} LuaScript", scriptCount));
                        if (physicsCount > 0)compNames.push_back(fmt::format("{} Rigidbody2D", physicsCount));

                        for (auto& name : compNames) {
                            ImGui::BulletText("%s", name.c_str());
                        }
                        ImGui::TreePop();
                    }
                } else {
                    ImGui::TextDisabled("Prefab not loaded");
                }
            }

            // ==========================================
            // ▼ File Info
            // ==========================================
            ImGui::Spacing();
            if (ImGui::CollapsingHeader("File Info", ImGuiTreeNodeFlags_DefaultOpen)) {
                float labelW = ImGui::CalcTextSize("Virtual Path").x + 16.0f * uiScale;
                float maxValX = ImGui::GetWindowContentRegionMax().x - 4.0f * uiScale;
                auto Row = [&](const char* label, const char* val) {
                    ImGui::Text("%s", label);
                    ImGui::SameLine(labelW);
                    ImGui::PushTextWrapPos(maxValX);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "%s", val);
                    ImGui::PopTextWrapPos();
                };
                Row("Virtual Path", meta.VirtualPath.c_str());

                std::string physPath = AssetManager::GetAssetPhysicalPath(inspectHandle);
                if (!physPath.empty()) Row("Physical Path", physPath.c_str());
                else Row("Physical Path", "(virtual / embedded)");
            }

            // ==========================================
            // ▼ Preview — optimized: cached thumbnail by default, realtime only during drag
            // ==========================================
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float availWidth = ImGui::GetContentRegionAvail().x;
            ImVec2 previewSize(availWidth, availWidth);

            static glm::vec2 s_PrefabPreviewRotation(0.3f, -0.6f);
            static UUID s_LastPrefabAsset = 0;
            if (s_LastPrefabAsset != inspectHandle) {
                s_PrefabPreviewRotation = glm::vec2(0.3f, -0.6f);
                s_LastPrefabAsset = inspectHandle;
            }

            auto previewTex = AssetPreviewer::RenderRealtimePreviewForPrefab(inspectHandle, s_PrefabPreviewRotation, 256);

            if (previewTex) {
                bool isVulkan = RendererAPI::GetAPI() == RendererAPI::API::Vulkan;
                ImVec2 uv0 = isVulkan ? ImVec2(0, 0) : ImVec2(0, 1);
                ImVec2 uv1 = isVulkan ? ImVec2(1, 1) : ImVec2(1, 0);
                ImGui::Image((ImTextureID)previewTex->GetImGuiTextureID(), previewSize, uv0, uv1);

                ImVec2 imgPos = ImGui::GetItemRectMin();
                ImGui::SetCursorScreenPos(imgPos);
                ImGui::InvisibleButton("##PrefabPreviewDrag", previewSize, ImGuiButtonFlags_MouseButtonLeft);
                if (ImGui::IsItemActivated() || ImGui::IsItemDeactivated()) {
                    s_PrefabPreviewRotation = glm::vec2(0.3f, -0.6f);
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                }
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                    s_PrefabPreviewRotation.x += delta.y * 0.01f;
                    s_PrefabPreviewRotation.y -= delta.x * 0.01f;
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                }
            } else {
                ImVec2 cursor = ImGui::GetCursorPos();
                ImGui::Dummy(previewSize);
                ImGui::SetCursorPos(cursor);
                ImGui::TextDisabled("Loading...");
            }
        } else if (meta.Type == AssetType::Material) {
            auto mat = AssetManager::GetAsset<Material>(m_SelectedAsset);
            if (!mat) {
                ImGui::TextDisabled("Material not loaded");
            } else {
                float uiScale = ImGui::GetIO().FontGlobalScale;
                ImGui::Text("Shader: %s", mat->ShaderName.c_str());
                const char* blendNames[] = { "Opaque", "Masked", "Translucent" };
                int blendIdx = (int)mat->GetBlendMode();
                ImGui::Text("Blend: %s", blendIdx < 3 ? blendNames[blendIdx] : "Unknown");
                if (mat->GetBlendMode() == MaterialBlendMode::Masked)
                    ImGui::Text("Alpha Cutoff: %.2f", mat->GetAlphaCutoff());
                ImGui::Separator();

                // Group properties by category
                std::string currentCategory;
                auto beginCategory = [&](const std::string& cat) {
                    if (currentCategory != cat) {
                        currentCategory = cat;
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "%s", cat.c_str());
                    }
                };
                auto categorize = [](const std::string& name) -> std::string {
                    if (name.find("Albedo") != std::string::npos) return "Albedo";
                    if (name.find("Normal") != std::string::npos) return "Normal";
                    if (name.find("ORM") != std::string::npos) return "ORM (Packed)";
                    if (name.find("Metallic") != std::string::npos) return "Metallic";
                    if (name.find("Roughness") != std::string::npos) return "Roughness";
                    if (name.find("AO") != std::string::npos || name.find("Ambient") != std::string::npos) return "AO";
                    if (name.find("Height") != std::string::npos || name.find("Displace") != std::string::npos) return "Height";
                    if (name.find("Emissive") != std::string::npos || name.find("Emission") != std::string::npos) return "Emissive";
                    if (name.find("Alpha") != std::string::npos || name.find("Opacity") != std::string::npos) return "Alpha / Opacity";
                    return "Other";
                };

                currentCategory.clear();
                for (auto& prop : mat->Properties) {
                    beginCategory(categorize(prop.UniformName));

                    ImGui::PushID(prop.UniformName.c_str());
                    std::string label = prop.DisplayName.empty() ? prop.UniformName : prop.DisplayName;
                    switch (prop.Type) {
                    case MaterialPropertyType::Texture2D: {
                        auto tex = AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                        if (tex) {
                            ImVec2 uv0(0, 0), uv1(1, 1);
                            if (tex->IsDataFlipped()) { uv0.y = 1; uv1.y = 0; }
                            float s = 48.0f * uiScale;
                            ImGui::Image((ImTextureID)tex->GetImGuiTextureID(), {s, s}, uv0, uv1);
                            ImGui::SameLine();
                            ImGui::Text("%s", label.c_str());
                            ImGui::SameLine();
                            ImGui::TextDisabled("(Texture)");
                        } else {
                            ImGui::Text("%s: (not loaded)", label.c_str());
                        }
                        break;
                    }
                    case MaterialPropertyType::Float:
                        ImGui::Text("%s: %.3f", label.c_str(), prop.FloatValue);
                        break;
                    case MaterialPropertyType::Bool:
                        ImGui::Text("%s: %s", label.c_str(), prop.BoolValue ? "On" : "Off");
                        break;
                    case MaterialPropertyType::Vec3:
                        ImGui::Text("%s: (%.2f, %.2f, %.2f)", label.c_str(),
                                    prop.Vec3Value.x, prop.Vec3Value.y, prop.Vec3Value.z);
                        break;
                    case MaterialPropertyType::Vec4:
                        ImGui::Text("%s: (%.2f, %.2f, %.2f, %.2f)", label.c_str(),
                                    prop.Vec4Value.x, prop.Vec4Value.y, prop.Vec4Value.z, prop.Vec4Value.w);
                        break;
                    default:
                        ImGui::Text("%s", label.c_str());
                        break;
                    }
                    ImGui::PopID();
                }
            }
        } else if (meta.Type == AssetType::Curve) {
            auto curve = AssetManager::GetAsset<CurveAsset>(m_SelectedAsset);
            if (!curve) {
                ImGui::TextDisabled("Curve not loaded");
            } else {
                ImGui::Text("Keyframes: %zu", curve->Keys.size());
                float minT = curve->Keys.empty() ? 0.0f : curve->Keys[0].Time;
                float maxT = curve->Keys.empty() ? 1.0f : curve->Keys.back().Time;
                ImGui::Text("Time Range: [%.2f, %.2f]", minT, maxT);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Keyframe list (read-only, first 10)
                int showCount = std::min((int)curve->Keys.size(), 10);
                for (int i = 0; i < showCount; i++) {
                    auto& k = curve->Keys[i];
                    ImGui::Text("  #%d: t=%.3f  v=%.3f", i, k.Time, k.Value);
                }
                if ((int)curve->Keys.size() > 10)
                    ImGui::TextDisabled("  ... and %zu more", curve->Keys.size() - 10);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("Open in Curve Editor", ImVec2(-1, 0))) {
                    std::string phys = VFS::ResolveString(meta.VirtualPath);
                    EditorLayer::Get().GetCurveEditorPanel().OpenCurve(curve, phys);
                }
            }
        } else {
            ImGui::TextDisabled("No import settings available for this asset type");
        }
    }


}
