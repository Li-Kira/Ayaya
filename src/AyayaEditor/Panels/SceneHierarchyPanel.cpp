#include "ayapch.h"
#include "../EditorLayer.hpp"
#include "SceneHierarchyPanel.hpp"
#include "Engine/Scene/Components.hpp"
#include "Engine/Core/EditorCommands.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Texture.hpp"
#include "Renderer/MaterialSerializer.hpp"


#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

// --- 引入 FontAwesome 图标宏 ---
#include <IconsFontAwesome5.h> 

namespace Ayaya {

    // ==========================================
    // 静态辅助函数：用于动态生成默认的白色 PBR 材质
    // ==========================================
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

    SceneHierarchyPanel::SceneHierarchyPanel(const std::shared_ptr<Scene>& context) {
        SetContext(context);
    }

    void SceneHierarchyPanel::SetContext(const std::shared_ptr<Scene>& context) {
        m_Context = context;
        ClearSelection(); // 切换场景时清空所有选中状态
    }

    void SceneHierarchyPanel::OnImGuiRender() {
        m_TextureGarbageBin.clear();
        
        ImGui::Begin("Properties");
        if (!m_SelectedEntities.empty()) {
            DrawComponents(); 
        }
        ImGui::End();

        ImGui::Begin("Scene Hierarchy");
        float uiScale = ImGui::GetIO().FontGlobalScale;

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
                // --- 新增 3D 几何体分类 ---
                if (ImGui::BeginMenu("3D Object")) {
                    if (ImGui::MenuItem("Cube")) {
                        Entity entity = m_Context->CreateEntity("Cube");
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        mrc.ModelAsset = std::make_shared<Model>(Mesh::CreateCube(1.0f));
                        mrc.ModelAsset->SetPath("Primitive::Cube"); // 打上标记，防止丢失
                        mrc.MaterialAsset = CreateWhitePBR();       // 赋予白模材质
                        SetSelectedEntity(entity);
                    }
                    if (ImGui::MenuItem("Sphere")) {
                        Entity entity = m_Context->CreateEntity("Sphere");
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        mrc.ModelAsset = std::make_shared<Model>(Mesh::CreateSphere(0.5f, 64, 64));
                        mrc.ModelAsset->SetPath("Primitive::Sphere");
                        mrc.MaterialAsset = CreateWhitePBR();
                        SetSelectedEntity(entity);
                    }
                    if (ImGui::MenuItem("Plane")) {
                        Entity entity = m_Context->CreateEntity("Plane");
                        auto& mrc = entity.AddComponent<MeshRendererComponent>();
                        mrc.ModelAsset = std::make_shared<Model>(Mesh::CreatePlane(1.0f, 1.0f));
                        mrc.ModelAsset->SetPath("Primitive::Plane");
                        mrc.MaterialAsset = CreateWhitePBR();
                        SetSelectedEntity(entity);
                    }
                    ImGui::EndMenu();
                }

                // ==========================================
                // 【新增】：一键创建天空盒！
                // ==========================================
                if (ImGui::MenuItem("Create Skybox")) {
                    Entity skyEntity = m_Context->CreateEntity("Skybox");
                    
                    // 自动挂载环境组件
                    auto& envComp = skyEntity.AddComponent<EnvironmentComponent>();
                    
                    // 给一个比较合理的默认初始状态 (比如默认不开启，等用户自己拖贴图)
                    envComp.Type = EnvironmentType::None; 
                    envComp.Intensity = 30000.0f;
                    envComp.AmbientColor = { 0.0f, 0.0f, 0.0f };
                    
                    // 创建完毕后，自动将其设为当前选中项，方便美术直接在属性面板操作
                    m_SelectedEntities.clear();
                    m_SelectedEntities.push_back(skyEntity);
                }
                // --- 新增灯光分类 ---
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

                // --- 新增相机 ---
                if (ImGui::MenuItem("Camera")) {
                    Entity entity = m_Context->CreateEntity("Camera");
                    auto& cc = entity.AddComponent<CameraComponent>();
                    cc.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
                    cc.Primary = false; // 新建相机默认不顶替主相机
                    SetSelectedEntity(entity);
                }

                // ==========================================
                // 【新增】：一键创建后处理体积对象！
                // ==========================================
                if (ImGui::MenuItem("Post Process Volume")) {
                    Entity ppvEntity = m_Context->CreateEntity("Post Process Volume");
                    
                    // 自动挂载后处理体积组件
                    ppvEntity.AddComponent<PostProcessVolumeComponent>();
                    
                    // 创建完毕后自动选中，方便立即编辑参数
                    SetSelectedEntity(ppvEntity);
                }

                ImGui::EndPopup();
            }

            // ==========================================
            // [交互 3]：处理拖放逻辑 (实体解绑 & 模型实例化)
            // ==========================================
            if (ImGui::BeginDragDropTarget()) {
                
                // 拦截 1：实体拖放到空白处 -> 解除父子关系，变为根节点
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_PAYLOAD")) {
                    entt::entity droppedID = *(entt::entity*)payload->Data;
                    m_EntitiesToUnparent.push_back({ droppedID, m_Context.get() }); 
                }

                // ==========================================
                // 【核心新增】：拦截 2：从资源管理器拖入 3D 模型文件 -> 在场景中实例化整棵树！
                // ==========================================
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* pathStr = (const char*)payload->Data;
                    std::filesystem::path modelPath = std::filesystem::path("assets") / pathStr;
                    
                    if (modelPath.extension() == ".obj" || modelPath.extension() == ".fbx" || modelPath.extension() == ".gltf") {
                        AYAYA_CORE_INFO("Instantiating Model to Scene: {0}", modelPath.string());
                        
                        // 1. 加载模型结构
                        auto loadedModel = std::make_shared<Model>(modelPath.string());
                        
                        // 2. 调用场景实例化接口，自动生成树状层级实体
                        Entity rootEntity = m_Context->InstantiateModel(loadedModel);
                        
                        // 3. 自动选中新生成的模型根节点
                        if (rootEntity) {
                            SetSelectedEntity(rootEntity);
                        }
                    }
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
        else if (entity.HasComponent<DirectionalLightComponent>()) icon = ICON_FA_SUN; 
        else if (entity.HasComponent<PointLightComponent>()) icon = ICON_FA_LIGHTBULB;
        else if (entity.HasComponent<EnvironmentComponent>()) icon = ICON_FA_CLOUD_SUN; 
        else if (entity.HasComponent<PostProcessVolumeComponent>()) icon = ICON_FA_MAGIC;

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

        float uiScale = ImGui::GetIO().FontGlobalScale;

        // 处理节点右侧的可视化按钮
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24.0f * uiScale);
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
        
        if (ImGui::Button(eyeIcon.c_str(), ImVec2(24.0f * uiScale, ImGui::GetTextLineHeight()))) {
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

        float uiScale = ImGui::GetIO().FontGlobalScale;

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
                if (UI::DrawVec3Control("Position", translation, 0.0f, 100.0f, &activated, &deactivated)) {
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
                if (UI::DrawVec3Control("Rotation", rotation, 0.0f, 100.0f, &activated, &deactivated)) {
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
                if (UI::DrawVec3Control("Scale", scale, 1.0f, 100.0f, &activated, &deactivated)) {
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
                    ImGui::Image((ImTextureID)(intptr_t)tex->GetRendererID(), textureSlotSize, {0, 1}, {1, 0});
                } else {
                    ImGui::Button("Empty", textureSlotSize);
                }

                // ------------------------------------------
                // 2. 贴图拖放逻辑 (带撤回打包)
                // ------------------------------------------
                if (ImGui::BeginDragDropTarget()) { 
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = (const char*)payload->Data;
                        std::filesystem::path texturePath = std::filesystem::path("assets") / pathStr;

                        if (texturePath.extension() == ".png" || texturePath.extension() == ".jpg") {
                            UUID importedHandle = AssetManager::ImportAsset(texturePath);
                            if (importedHandle != 0) {
                                
                                // 【撤回拦截】：拖放瞬间，备份旧状态
                                std::vector<SpriteRendererComponent> oldComps;
                                for (auto e : m_SelectedEntities) oldComps.push_back(e.GetComponent<SpriteRendererComponent>());

                                // 批量应用新贴图
                                for (auto e : m_SelectedEntities) {
                                    e.GetComponent<SpriteRendererComponent>().TextureHandle = importedHandle;
                                }

                                // 提交撤回命令
                                auto macroCmd = std::make_shared<MacroCommand>("Assign Sprite Texture to " + getTargetName());
                                for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                                    macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<SpriteRendererComponent>>(
                                        m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<SpriteRendererComponent>()
                                    ));
                                }
                                EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);

                                AYAYA_CORE_INFO("Successfully imported and applied texture: {0}", texturePath.string());
                            }
                        } else {
                            AYAYA_CORE_WARN("Dropped file is not a supported image format!");
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
                            if (!IsEntitySelected(e)) e.GetComponent<CameraComponent>().Primary = false;
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
                    
                    // 【核心魔法】：强行“弄脏”备份数据！保证撤回到旧状态时触发渲染器重新加载！
                    for (auto& c : oldComps) c.IsDirty = true;

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
                        std::string pathDisplay = refEnv.EquirectangularPath.empty() ? "Drop .hdr / .jpg here" : refEnv.EquirectangularPath;
                        
                        ImGui::Button(pathDisplay.c_str(), ImVec2(-1.0f, 30.0f));

                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                const char* pathStr = (const char*)payload->Data;
                                std::filesystem::path texPath = std::filesystem::path("assets") / pathStr;
                                
                                if (texPath.extension() == ".hdr" || texPath.extension() == ".jpg" || texPath.extension() == ".png") {
                                    
                                    std::vector<EnvironmentComponent> oldComps = pureOldEnvs;
                                    // 【核心魔法】：同样强行弄脏旧状态
                                    for (auto& c : oldComps) c.IsDirty = true;

                                    for (auto e : m_SelectedEntities) {
                                        auto& comp = e.GetComponent<EnvironmentComponent>();
                                        comp.EquirectangularPath = texPath.string();
                                        
                                        if (texPath.extension() == ".hdr") comp.Type = EnvironmentType::HDR_Equirectangular;
                                        else comp.Type = EnvironmentType::LDR_Equirectangular;
                                        
                                        comp.EquirectangularTexture = Texture2D::Create(comp.EquirectangularPath);
                                        comp.IsDirty = true;
                                    }

                                    auto macroCmd = std::make_shared<MacroCommand>("Assign Equirectangular Map to " + getTargetName());
                                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<EnvironmentComponent>>(
                                            m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<EnvironmentComponent>()
                                        ));
                                    }
                                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);

                                } else {
                                    AYAYA_CORE_WARN("Invalid environment map format! Please use .hdr or .jpg/.png");
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                    } 
                    // ------------------------------------------
                    // 4. 传统 6 面体贴图
                    // ------------------------------------------
                    else if (refEnv.Type == EnvironmentType::Classic_Cubemap) {
                        ImGui::Spacing();
                        ImGui::Text("Cubemap Faces (Drag & Drop .jpg/.png)");
                        const char* faceNames[] = { "Right (+X)", "Left (-X)", "Top (+Y)", "Bottom (-Y)", "Front (+Z)", "Back (-Z)" };
                        
                        for(int i = 0; i < 6; ++i) {
                            ImGui::PushID(i);
                            ImGui::Text("%s", faceNames[i]);
                            
                            std::string faceDisplay = refEnv.CubemapFaces[i].empty() ? "Drop Image Here" : refEnv.CubemapFaces[i];
                            ImGui::Button(faceDisplay.c_str(), ImVec2(-1.0f, 30.0f));

                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                    const char* pathStr = (const char*)payload->Data;
                                    std::filesystem::path texPath = std::filesystem::path("assets") / pathStr;
                                    
                                    if (texPath.extension() == ".jpg" || texPath.extension() == ".png") {
                                        std::vector<EnvironmentComponent> oldComps = pureOldEnvs;

                                        for (auto e : m_SelectedEntities) {
                                            e.GetComponent<EnvironmentComponent>().CubemapFaces[i] = texPath.string();
                                        }

                                        auto macroCmd = std::make_shared<MacroCommand>("Assign Cubemap Face " + std::string(faceNames[i]) + " to " + getTargetName());
                                        for (size_t j = 0; j < m_SelectedEntities.size(); ++j) {
                                            macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<EnvironmentComponent>>(
                                                m_SelectedEntities[j], oldComps[j], m_SelectedEntities[j].GetComponent<EnvironmentComponent>()
                                            ));
                                        }
                                        EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);

                                    } else {
                                        AYAYA_CORE_WARN("Invalid cubemap face format! Please use .jpg or .png");
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }
                            ImGui::PopID();
                        }
                        
                        ImGui::Spacing();
                        if (ImGui::Button("Load Cubemap & Bake", ImVec2(-1.0f, 35.0f))) {
                            std::vector<EnvironmentComponent> oldComps = pureOldEnvs;
                            // 【核心魔法】：撤回“烘焙操作”时，也要触发底层状态刷新
                            for (auto& c : oldComps) c.IsDirty = true; 

                            bool allFacesPresent = true;
                            for (auto e : m_SelectedEntities) {
                                auto& comp = e.GetComponent<EnvironmentComponent>();
                                for (int i = 0; i < 6; i++) {
                                    if (comp.CubemapFaces[i].empty()) allFacesPresent = false;
                                }
                                
                                if (allFacesPresent) {
                                    comp.ClassicCubemapTexture = TextureCube::Create(comp.CubemapFaces);
                                    comp.IsDirty = true; 
                                } else {
                                    AYAYA_CORE_WARN("Please assign all 6 faces before baking!");
                                }
                            }

                            if (allFacesPresent) {
                                auto macroCmd = std::make_shared<MacroCommand>("Bake Cubemap Environment of " + getTargetName());
                                for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                                    macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<EnvironmentComponent>>(
                                        m_SelectedEntities[i], oldComps[i], m_SelectedEntities[i].GetComponent<EnvironmentComponent>()
                                    ));
                                }
                                EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                            }
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
            bool opened = ImGui::TreeNodeEx((void*)typeid(MeshRendererComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_CUBE " Mesh Renderer");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            
            bool removeComponent = false;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove Component")) removeComponent = true;
                ImGui::EndPopup();
            }

            if (opened) {
                auto& refMrc = referenceEntity.GetComponent<MeshRendererComponent>();

                // 动态名字生成器
                auto getTargetName = [&]() -> std::string {
                    if (m_SelectedEntities.size() == 1) return "'" + m_SelectedEntities[0].GetComponent<TagComponent>().Tag + "'";
                    return std::to_string(m_SelectedEntities.size()) + " Entities";
                };

                // 【物理快照】：在任何交互前捕获绝对纯净的旧状态
                std::vector<MeshRendererComponent> pureOldMrcs;
                for (auto e : m_SelectedEntities) pureOldMrcs.push_back(e.GetComponent<MeshRendererComponent>());

                // 【终极重用】：一个用于打包“瞬时操作（如拖入文件、点击按钮）”的辅助函数
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
                    if (refMrc.ModelAsset) {
                        std::string path = refMrc.ModelAsset->GetPath();
                        if (!path.empty()) {
                            modelDisplay = path;
                        } else {
                            modelDisplay = "Primitive::Cube";
                            refMrc.ModelAsset->SetPath("Primitive::Cube"); 
                        }
                    }

                    if (ImGui::Button(modelDisplay.c_str(), ImVec2(-1.0f, 30.0f))) {
                        ImGui::OpenPopup("ModelSelectionPopup");
                    }

                    // 拦截 1：拖放模型
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            const char* pathStr = (const char*)payload->Data;
                            std::filesystem::path modelPath = std::filesystem::path("assets") / pathStr;
                            if (modelPath.extension() == ".obj" || modelPath.extension() == ".fbx" || modelPath.extension() == ".gltf") {
                                
                                std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                                for (auto e : m_SelectedEntities) {
                                    e.GetComponent<MeshRendererComponent>().ModelAsset = std::make_shared<Model>(modelPath.string());
                                }
                                commitInstantCommand("Assign Model", oldComps);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // 拦截 2：内置几何体切换
                    if (ImGui::BeginPopup("ModelSelectionPopup")) {
                        ImGui::TextDisabled("Built-in Primitives");
                        ImGui::Separator();
                        
                        if (ImGui::MenuItem("Cube")) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) {
                                auto model = std::make_shared<Model>(Mesh::CreateCube(1.0f));
                                model->SetPath("Primitive::Cube");
                                e.GetComponent<MeshRendererComponent>().ModelAsset = model;
                            }
                            commitInstantCommand("Assign Cube", oldComps);
                        }
                        if (ImGui::MenuItem("Sphere")) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) {
                                auto model = std::make_shared<Model>(Mesh::CreateSphere(0.5f, 64, 64));
                                model->SetPath("Primitive::Sphere");
                                e.GetComponent<MeshRendererComponent>().ModelAsset = model;
                            }
                            commitInstantCommand("Assign Sphere", oldComps);
                        }
                        if (ImGui::MenuItem("Plane")) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) {
                                auto model = std::make_shared<Model>(Mesh::CreatePlane(1.0f, 1.0f));
                                model->SetPath("Primitive::Plane");
                                e.GetComponent<MeshRendererComponent>().ModelAsset = model;
                            }
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
                    
                    if (refMrc.MaterialAsset) {
                        auto& mat = refMrc.MaterialAsset;
                        
                        ImGui::Text("Material Asset (.mat)");
                        std::string matDisplay = (!mat->AssetPath.empty()) ? mat->AssetPath : "Default / Internal";
                        ImGui::Button(matDisplay.c_str(), ImVec2(-1.0f, 30.0f));

                        // 拦截 3：批量拖拽应用材质
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                const char* pathStr = (const char*)payload->Data;
                                std::filesystem::path matPath = std::filesystem::path("assets") / pathStr;
                                if (matPath.extension() == ".mat") {
                                    
                                    std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                                    for (auto e : m_SelectedEntities) {
                                        auto newMat = std::make_shared<Material>();
                                        if (MaterialSerializer::Deserialize(newMat, matPath.string())) {
                                            e.GetComponent<MeshRendererComponent>().MaterialAsset = newMat; 
                                        }
                                    }
                                    commitInstantCommand("Assign Material", oldComps);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        // 保存到本地的操作不涉及组件级数据修改，无需撤回
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

                        // 拦截 4：移除材质
                        if (ImGui::Button("Remove Material")) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) e.GetComponent<MeshRendererComponent>().MaterialAsset = nullptr;
                            commitInstantCommand("Remove Material", oldComps);
                        }

                        if (refMrc.MaterialAsset) { 
                            ImGui::Text("Shader: %s", refMrc.MaterialAsset->ShaderName.c_str());
                            ImGui::Separator();

                            ImGui::Columns(2, "MaterialProperties", false);
                            ImGui::SetColumnWidth(0, 140.0f * uiScale);
                            std::string lastCategory = ""; 

                            // 材质属性修改区：保留原有逻辑
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
                                    case MaterialPropertyType::Float: propChanged = ImGui::SliderFloat("##val", &prop.FloatValue, 0.0f, 1.0f); break;
                                    case MaterialPropertyType::Int: propChanged = ImGui::InputInt("##val", &prop.IntValue); break;
                                    case MaterialPropertyType::Bool: propChanged = ImGui::Checkbox("##val", &prop.BoolValue); break;
                                    case MaterialPropertyType::Vec2: propChanged = ImGui::DragFloat2("##val", glm::value_ptr(prop.Vec2Value), 0.05f); break;
                                    case MaterialPropertyType::Vec3: propChanged = ImGui::ColorEdit3("##val", glm::value_ptr(prop.Vec3Value), ImGuiColorEditFlags_NoInputs); break;
                                    case MaterialPropertyType::Vec4: propChanged = ImGui::ColorEdit4("##val", glm::value_ptr(prop.Vec4Value), ImGuiColorEditFlags_NoInputs); break;
                                    case MaterialPropertyType::Texture2D:
                                    {
                                        ImVec2 textureSlotSize = { 64.0f * uiScale, 64.0f * uiScale };
                                        if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                            auto tex = AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                                            
                                            // ==========================================
                                            // 【修复 1 & 2】：跨平台 ID 与动态 UV 翻转
                                            // ==========================================
                                            ImVec2 uv0 = tex->IsDataFlipped() ? ImVec2(0, 1) : ImVec2(0, 0);
                                            ImVec2 uv1 = tex->IsDataFlipped() ? ImVec2(1, 0) : ImVec2(1, 1);
                                            ImGui::Image((ImTextureID)tex->GetImGuiTextureID(), textureSlotSize, uv0, uv1);
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
                                                        
                                                        // ==========================================
                                                        // 【修复 3-A】：覆盖前，把旧贴图扔进垃圾桶续命！
                                                        // ==========================================
                                                        if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                                            m_TextureGarbageBin.push_back(AssetManager::GetAsset<Texture2D>(prop.TextureHandle));
                                                        }

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
                                                
                                                // ==========================================
                                                // 【修复 3-B】：移除前，把旧贴图扔进垃圾桶续命！
                                                // ==========================================
                                                if (AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                                                    m_TextureGarbageBin.push_back(AssetManager::GetAsset<Texture2D>(prop.TextureHandle));
                                                }

                                                prop.TextureHandle = 0;
                                                prop.TexturePath = "";
                                                propChanged = true;
                                            }
                                        }
                                        break;
                                    }
                                    default: break;
                                }

                                if (propChanged) {
                                    for (auto e : m_SelectedEntities) {
                                        if (e.HasComponent<MeshRendererComponent>()) {
                                            auto currentMat = e.GetComponent<MeshRendererComponent>().MaterialAsset;
                                            if (currentMat) {
                                                for (auto& p : currentMat->Properties) {
                                                    if (p.UniformName == prop.UniformName) {
                                                        p = prop; 
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
                        // 拦截 5：添加默认材质
                        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.6f, 1.0f), "Warning: No Material Assigned!");
                        ImGui::PopFont();
                        if (ImGui::Button("Add Default Material", ImVec2(-1.0f, 30.0f))) {
                            std::vector<MeshRendererComponent> oldComps = pureOldMrcs;
                            for (auto e : m_SelectedEntities) {
                                auto templateMat = std::make_shared<Material>();
                                if (MaterialSerializer::Deserialize(templateMat, "assets/Editor/materials/DefaultPBR.mat")) {
                                    e.GetComponent<MeshRendererComponent>().MaterialAsset = templateMat->Clone();
                                } else {
                                    e.GetComponent<MeshRendererComponent>().MaterialAsset = std::make_shared<Material>();
                                }
                            }
                            commitInstantCommand("Add Default Material", oldComps);
                        }
                    }

                    // ==========================================
                    // 3. 投影管理参数
                    // ==========================================
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    // 拦截 6：阴影开关
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

                ImGui::TreePop();
            }

            // 处理组件移除结算
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<PostProcessVolumeComponent>();
            }
        }

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
                std::string pathDisplay = refLsc.ScriptPath.empty() ? "Drop .lua file here" : refLsc.ScriptPath;
                ImGui::Button(pathDisplay.c_str(), ImVec2(-1.0f, 30.0f));

                // ==========================================
                // 交互 1：支持拖拽 .lua 文件绑定脚本 (瞬时命令打包)
                // ==========================================
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = (const char*)payload->Data;
                        std::filesystem::path scriptPath = std::filesystem::path("assets") / pathStr;
                        if (scriptPath.extension() == ".lua") {
                            
                            std::vector<LuaScriptComponent> oldComps = pureOldLscs;
                            
                            for (auto e : m_SelectedEntities) {
                                e.GetComponent<LuaScriptComponent>().ScriptPath = scriptPath.string();
                            }

                            // 提交拖放撤回指令
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
                // 交互 2：允许用户手动输入路径 (持续状态拦截)
                // ==========================================
                char buffer[256];
                memset(buffer, 0, sizeof(buffer));
                strncpy(buffer, refLsc.ScriptPath.c_str(), sizeof(buffer) - 1);
                
                static std::vector<LuaScriptComponent> s_OldLscs;

                // 实时应用输入
                if (ImGui::InputText("Path##ScriptPath", buffer, sizeof(buffer))) {
                    for (auto e : m_SelectedEntities) {
                        e.GetComponent<LuaScriptComponent>().ScriptPath = std::string(buffer);
                    }
                }

                // 拦截开始：鼠标点进输入框的瞬间，加载快照
                if (ImGui::IsItemActivated()) {
                    s_OldLscs = pureOldLscs;
                }

                // 拦截结束：按下回车或点击空白处结束编辑瞬间，提交指令
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    auto macroCmd = std::make_shared<MacroCommand>("Change Lua Script Path of " + getTargetName());
                    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
                        macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<LuaScriptComponent>>(
                            m_SelectedEntities[i], s_OldLscs[i], m_SelectedEntities[i].GetComponent<LuaScriptComponent>()
                        ));
                    }
                    EditorLayer::Get().GetCommandHistory().AddCommand(macroCmd);
                }

                ImGui::TreePop();
            }
            if (removeComponent) {
                for (auto e : m_SelectedEntities) e.RemoveComponent<LuaScriptComponent>();
            }
        }

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
            
            ImGui::EndPopup();
        }
    }

}