#include "ayapch.h"
#include "SceneHierarchyPanel.hpp"
#include "Engine/Scene/Components.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Texture.hpp"

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
        else if (entity.HasComponent<MeshRendererComponent>()) icon = ICON_FA_PAINT_BRUSH;
        else if (entity.HasComponent<DirectionalLightComponent>()) icon = ICON_FA_LIGHTBULB; 

        
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
        // ==========================================
        // 绘制组件
        // ==========================================

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

                // ==========================================
                // 贴图槽位 UI 与拖拽接收 (Drop Target) 逻辑
                // ==========================================
                ImGui::Spacing();
                ImGui::Text("Texture");

                ImVec2 textureSlotSize = { 64.0f, 64.0f };
                
                // 1. 绘制展示框：有贴图就画贴图，没贴图就画个空按钮框
                if (src.TextureHandle != 0 && AssetManager::IsAssetHandleValid(src.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(src.TextureHandle);
                    // 注意 UV 的翻转：{0, 1}, {1, 0}
                    ImGui::Image((ImTextureID)(intptr_t)tex->GetRendererID(), textureSlotSize, {0, 1}, {1, 0});
                } else {
                    ImGui::Button("Empty", textureSlotSize);
                }

                // 2. 核心魔法：声明刚刚画的展示框是一个可以接收“拖拽包裹”的坑！
                if (ImGui::BeginDragDropTarget()) { // <==== 【务必加上这一行！】开启接收坑
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = (const char*)payload->Data;
                        std::filesystem::path texturePath = std::filesystem::path("assets") / pathStr;

                        if (texturePath.extension() == ".png" || texturePath.extension() == ".jpg") {
                            // 一行代码搞定：导入资产、写入账本、返回全局唯一 UUID
                            UUID importedHandle = AssetManager::ImportAsset(texturePath);
                            
                            if (importedHandle != 0) {
                                src.TextureHandle = importedHandle;
                                AYAYA_CORE_INFO("Successfully imported and applied texture: {0}", texturePath.string());
                            }
                        } else {
                            AYAYA_CORE_WARN("Dropped file is not a supported image format!");
                        }
                    }
                    ImGui::EndDragDropTarget(); // <==== 【务必加上这一行！】结束接收坑
                }

                // 3. 卸载贴图的小功能
                if (src.TextureHandle != 0) {
                    ImGui::SameLine();
                    // 稍微算一下坐标，让 Remove 按钮和 64x64 的图片垂直居中对齐
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textureSlotSize.y * 0.5f - 10.0f);
                    if (ImGui::Button("Remove")) {
                        src.TextureHandle = 0; // 只要把 UUID 设为 0，渲染器就会自动用白底替代
                    }
                }

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

        // --- 绘制 Mesh Renderer 组件 ---
        if (entity.HasComponent<MeshRendererComponent>()) {
            if (ImGui::TreeNodeEx((void*)typeid(MeshRendererComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Mesh Renderer")) {
                auto& mrc = entity.GetComponent<MeshRendererComponent>();
                ImGui::ColorEdit4("Color", glm::value_ptr(mrc.Color));

                // ==========================================
                // 新增：模型资产拖拽插槽 (Drag & Drop Target)
                // ==========================================
                ImGui::Spacing();
                ImGui::Text("Model Asset");
                
                // 画一个占满整行的按钮作为“接收区”
                ImGui::Button("Drop .obj / .fbx here", ImVec2(-1.0f, 30.0f));

                // 当有东西拖拽到这个按钮上时...
                if (ImGui::BeginDragDropTarget()) {
                    // 检查载荷类型是否为 CONTENT_BROWSER_ITEM (我们在 Content Browser 里定义的名字)
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        const char* pathStr = (const char*)payload->Data;
                        std::filesystem::path modelPath = std::filesystem::path("assets") / pathStr;
                        
                        // 校验后缀名：只接收常见的 3D 模型格式
                        if (modelPath.extension() == ".obj" || modelPath.extension() == ".fbx" || modelPath.extension() == ".gltf") {
                            AYAYA_CORE_INFO("Loading model: {0}", modelPath.string());
                            // 动态替换模型！
                            mrc.ModelAsset = std::make_shared<Model>(modelPath.string());
                        } else {
                            AYAYA_CORE_WARN("Unsupported model format!");
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::Spacing();
                ImGui::Text("Texture");

                ImVec2 textureSlotSize = { 64.0f, 64.0f };
                if (mrc.TextureHandle != 0 && AssetManager::IsAssetHandleValid(mrc.TextureHandle)) {
                    auto tex = AssetManager::GetAsset<Texture2D>(mrc.TextureHandle);
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
                                mrc.TextureHandle = importedHandle;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (mrc.TextureHandle != 0) {
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + textureSlotSize.y * 0.5f - 10.0f);
                    if (ImGui::Button("Remove")) mrc.TextureHandle = 0;
                }
                ImGui::TreePop();
            }
        }

        // --- 绘制 Directional Light 组件 ---
        if (entity.HasComponent<DirectionalLightComponent>()) {
            if (ImGui::TreeNodeEx((void*)typeid(DirectionalLightComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Directional Light")) {
                auto& dlc = entity.GetComponent<DirectionalLightComponent>();
                
                ImGui::ColorEdit3("Light Color", glm::value_ptr(dlc.Color));
                ImGui::DragFloat("Ambient Strength", &dlc.AmbientStrength, 0.01f, 0.0f, 1.0f);
                
                ImGui::TreePop();
            }
        }

        // ==========================================
        // “添加组件” 按钮与下拉菜单
        // ==========================================
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 按钮居中魔法
        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosX(contentRegionAvailable.x * 0.5f - 60.0f);
        
        if (ImGui::Button("Add Component", ImVec2(120, 25))) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            // 如果没有相机组件，才显示添加相机
            if (!entity.HasComponent<CameraComponent>()) {
                if (ImGui::MenuItem("Camera")) {
                    entity.AddComponent<CameraComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            // 如果没有 MeshRenderer，才显示添加 MeshRenderer
            if (!entity.HasComponent<MeshRendererComponent>()) {
                if (ImGui::MenuItem("Mesh Renderer")) {
                    entity.AddComponent<MeshRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            // 之前的 SpriteRenderer 也可以留着做 2D 用
            if (!entity.HasComponent<SpriteRendererComponent>()) {
                if (ImGui::MenuItem("Sprite Renderer")) {
                    entity.AddComponent<SpriteRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            // 如果没有平行光，才显示添加平行光
            if (!entity.HasComponent<DirectionalLightComponent>()) {
                if (ImGui::MenuItem("Directional Light")) {
                    entity.AddComponent<DirectionalLightComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

}