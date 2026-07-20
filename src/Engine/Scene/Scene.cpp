#include "ayapch.h"
#include "Scene.hpp"
#include "Entity.hpp"
#include "Components.hpp"
#include "Renderer/MaterialSerializer.hpp"
#include "UILayoutSystem.hpp"
#include "Core/Application.hpp"
#include "Asset/AssetManager.hpp"
#include "Asset/Prefab.hpp"

#include <box2d/b2_world.h>
#include <box2d/b2_body.h>
#include <box2d/b2_fixture.h>
#include <box2d/b2_polygon_shape.h>
#include "Scripting/ScriptEngine.hpp"
#include "Engine/Animation/AnimationSystem.hpp"
#include <glm/gtx/matrix_decompose.hpp>

namespace Ayaya {

    Scene::Scene() {}

    Scene::~Scene() {}

    // 原来的 CreateEntity 现在直接调用带有随机 UUID 的版本
    Entity Scene::CreateEntity(const std::string& name) {
        return CreateEntityWithUUID(UUID(), name);
    }

    // 将原来 CreateEntity 里的逻辑全部移到这里
    Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name) {
        Entity entity = { m_Registry.create(), this };
        
        // --- 核心：给实体挂载唯一的 ID 组件 ---
        entity.AddComponent<IDComponent>(uuid);
        
        entity.AddComponent<TransformComponent>();
        
        // 处理重名逻辑...
        std::string baseName = name.empty() ? "Entity" : name;
        std::string uniqueName = baseName;
        int counter = 1;
        while (true) {
            bool nameExists = false;
            auto view = m_Registry.view<TagComponent>();
            for (auto e : view) {
                if (view.get<TagComponent>(e).Tag == uniqueName) {
                    nameExists = true;
                    break;
                }
            }
            if (!nameExists) break; 
            uniqueName = baseName + " (" + std::to_string(counter) + ")";
            counter++;
        }

        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = uniqueName;
        
        entity.AddComponent<RelationshipComponent>();
        m_RootEntities.push_back(entity.m_EntityHandle);
        m_EntityMap[uuid] = entity.m_EntityHandle;

        return entity;
    }

    void Scene::Clear() {
        auto roots = m_RootEntities;  // copy — DestroyEntity mutates the vector
        for (auto handle : roots) {
            Entity e{ handle, this };
            DestroyEntity(e);
        }
        m_RootEntities.clear();
        m_EntityMap.clear();
    }

    void Scene::DestroyEntity(Entity entity) {
        auto& rel = entity.GetComponent<RelationshipComponent>();

        std::vector<entt::entity> childrenCopy = rel.Children;
        for (auto childID : childrenCopy) {
            DestroyEntity({ childID, this });
        }

        entity.SetParent({}); 

        auto it = std::find(m_RootEntities.begin(), m_RootEntities.end(), entity.m_EntityHandle);
        if (it != m_RootEntities.end()) m_RootEntities.erase(it);

        m_EntityMap.erase(entity.GetComponent<IDComponent>().ID);
        m_Registry.destroy(entity);
    }

    void Scene::PropagateActiveState(Entity entity) {
        if (!entity.HasComponent<RelationshipComponent>()) return;
        auto& rel = entity.GetComponent<RelationshipComponent>();

        bool parentActive = true;
        if (rel.Parent != entt::null) {
            Entity p{ rel.Parent, this };
            if (p.HasComponent<RelationshipComponent>())
                parentActive = p.GetComponent<RelationshipComponent>().CachedActiveInHierarchy;
        }

        bool selfActive = true;
        if (entity.HasComponent<TagComponent>())
            selfActive = entity.GetComponent<TagComponent>().IsActive;

        rel.CachedActiveInHierarchy = parentActive && selfActive;

        for (auto childHandle : rel.Children)
            PropagateActiveState(Entity{ childHandle, this });
    }

    void Scene::InvalidateAssetCache(UUID assetHandle) {
        auto view = m_Registry.view<MeshRendererComponent>();
        for (auto entityID : view) {
            auto& comp = view.get<MeshRendererComponent>(entityID);
            if (comp.MaterialHandle == assetHandle) comp.CachedMaterial = nullptr;
            if (comp.ModelHandle == assetHandle)    comp.CachedModel    = nullptr;
        }
    }

    Entity Scene::GetEntityByUUID(UUID uuid) {
        auto it = m_EntityMap.find(uuid);
        return (it != m_EntityMap.end()) ? Entity{it->second, this} : Entity{};
    }

    // ==========================================
    // 模型实例化逻辑
    // ==========================================
    Entity Scene::InstantiateModel(const std::shared_ptr<Model>& model) {
        return InstantiateModel(model, Entity{}); 
    }

    Entity Scene::InstantiateModel(const std::shared_ptr<Model>& model, Entity parentEntity) {
        if (!model) return {};
        return InstantiateModelNode(model->GetRootNode(), parentEntity);
    }

    Entity Scene::InstantiateModelNode(const ModelNode& node, Entity parentEntity) {
        // 1. 创建实体
        Entity entity = CreateEntity(node.Name.empty() ? "Model Node" : node.Name);

        // 2. 解析变换 (Translation/Rotation/Scale)
        auto& transform = entity.GetComponent<TransformComponent>();
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(node.LocalTransform, scale, rotation, translation, skew, perspective);
        
        transform.Translation = translation;
        transform.Rotation = glm::eulerAngles(rotation);
        transform.Scale = scale;

        // 3. 构建层级关系
        if (parentEntity) {
            auto& rel = entity.HasComponent<RelationshipComponent>() ? 
                        entity.GetComponent<RelationshipComponent>() : 
                        entity.AddComponent<RelationshipComponent>();
            rel.Parent = parentEntity.GetEntityHandle();

            auto& pRel = parentEntity.HasComponent<RelationshipComponent>() ? 
                        parentEntity.GetComponent<RelationshipComponent>() : 
                        parentEntity.AddComponent<RelationshipComponent>();
            pRel.Children.push_back(entity.GetEntityHandle());

            // 从根节点列表中移除（因为它现在是子节点了）
            auto it = std::find(m_RootEntities.begin(), m_RootEntities.end(), entity.GetEntityHandle());
            if (it != m_RootEntities.end()) m_RootEntities.erase(it);
        }

        // ==========================================
        // 【核心新增】：定义材质分配闭包
        // ==========================================
        auto ApplyDefaultMaterial = [](MeshRendererComponent& mrc) {
            // COW: share the read-only built-in material.  To customize, use the
            // editor's "Create Instance to Edit" button in the Properties panel.
            mrc.MaterialHandle = AssetManager::GetBuiltInMaterial();
        };

        // 4. 处理网格渲染组件
        if (!node.Meshes.empty()) {
            if (node.Meshes.size() == 1) {
                auto& meshComp = entity.HasComponent<MeshRendererComponent>() ? 
                                entity.GetComponent<MeshRendererComponent>() : 
                                entity.AddComponent<MeshRendererComponent>();
                
                // 【核心修复】：Model 不再有 Handle，由外部掌控 UUID！
                auto runtimeModel = std::make_shared<Model>(node.Meshes[0]);
                UUID runtimeModelHandle = UUID(); // 1. 生成一个独立的 UUID
                
                // 2. 使用新接口：同时传入 Handle 和 对象指针
                AssetManager::AddAsset(runtimeModelHandle, runtimeModel); 
                
                // 3. 将这个 UUID 记录在组件中
                meshComp.ModelHandle = runtimeModelHandle; 
                ApplyDefaultMaterial(meshComp);
            } else {
                for (size_t i = 0; i < node.Meshes.size(); i++) {
                    Entity subEntity = CreateEntity(node.Name + "_SubMesh_" + std::to_string(i));

                    auto& subRel = subEntity.AddComponent<RelationshipComponent>();
                    subRel.Parent = entity.GetEntityHandle();
                    auto it = std::find(m_RootEntities.begin(), m_RootEntities.end(), subEntity.GetEntityHandle());
                    if (it != m_RootEntities.end()) m_RootEntities.erase(it);

                    // Register sub-entity in parent's Children list (fix: was missing)
                    auto& pRel = entity.HasComponent<RelationshipComponent>() ?
                                entity.GetComponent<RelationshipComponent>() :
                                entity.AddComponent<RelationshipComponent>();
                    pRel.Children.push_back(subEntity.GetEntityHandle());

                    auto& meshComp = subEntity.AddComponent<MeshRendererComponent>();
                    
                    auto runtimeModel = std::make_shared<Model>(node.Meshes[i]);
                    UUID runtimeModelHandle = UUID(); 
                    
                    AssetManager::AddAsset(runtimeModelHandle, runtimeModel); 
                    
                    meshComp.ModelHandle = runtimeModelHandle;
                    ApplyDefaultMaterial(meshComp);
                }
            }
        }

        // 5. 递归处理所有子节点
        for (const auto& childNode : node.Children) {
            InstantiateModelNode(childNode, entity);
        }

        return entity;
    }

    // ============================================================
    // 泛型辅助函数：如果原实体有该组件，就完美拷贝给新实体
    // ============================================================
    template<typename T>
    static void CopyComponentIfExists(Entity dst, Entity src) {
        if (src.HasComponent<T>()) {
            dst.AddComponent<T>(src.GetComponent<T>());
        }
    }
    // ============================================================
    // 实现复制逻辑
    // ============================================================
    Entity Scene::DuplicateEntity(Entity entity) {
        std::string name = entity.GetComponent<TagComponent>().Tag;
        Entity newEntity = CreateEntity(name);

        // 强行覆盖 Transform
        newEntity.GetComponent<TransformComponent>() = entity.GetComponent<TransformComponent>();

        // 特殊处理 Camera (需要将新相机的 Primary 设为 false，防止抢占焦点)
        if (entity.HasComponent<CameraComponent>()) {
            auto& cameraComp = newEntity.AddComponent<CameraComponent>(entity.GetComponent<CameraComponent>());
            cameraComp.Primary = false; 
        }

        // =========================================================
        // 核心修复：利用模板一行代码无损拷贝所有新加入系统的组件！
        // =========================================================
        CopyComponentIfExists<SpriteRendererComponent>(newEntity, entity);
        CopyComponentIfExists<MeshRendererComponent>(newEntity, entity);
        CopyComponentIfExists<DirectionalLightComponent>(newEntity, entity);
        CopyComponentIfExists<PointLightComponent>(newEntity, entity);
        CopyComponentIfExists<SpotLightComponent>(newEntity, entity);
        CopyComponentIfExists<Rigidbody2DComponent>(newEntity, entity);
        CopyComponentIfExists<BoxCollider2DComponent>(newEntity, entity);
        CopyComponentIfExists<LuaScriptComponent>(newEntity, entity);
        CopyComponentIfExists<PostProcessVolumeComponent>(newEntity, entity);
        CopyComponentIfExists<EnvironmentComponent>(newEntity, entity);
        CopyComponentIfExists<AnimationControllerComponent>(newEntity, entity);

        // =========================================================
        // 【核心修复】：必须预先拷贝被复制物体的父节点和子节点数组！
        // 因为后续的 DuplicateEntity 递归会调用 CreateEntity，
        // 可能会引发 EnTT 底层内存重分配，导致直接引用原组件失效闪退。
        // =========================================================
        entt::entity parentHandle = entity.GetComponent<RelationshipComponent>().Parent;
        std::vector<entt::entity> childrenCopy = entity.GetComponent<RelationshipComponent>().Children;
        
        if (parentHandle != entt::null) {
            Entity parent{ parentHandle, this };
            newEntity.SetParent(parent, false); 
        }

        for (auto childID : childrenCopy) {
            Entity child{ childID, this };
            Entity newChild = DuplicateEntity(child);
            newChild.SetParent(newEntity, false);
        }

        return newEntity;
    }

    Entity Scene::InstantiatePrefab(Prefab* prefab) {
        if (!prefab || !prefab->GetScene()) return Entity{};
        Entity srcRoot = prefab->GetRootEntity();
        if (!srcRoot) return Entity{};

        Scene* srcScene = prefab->GetScene();

        // Recursive deep-clone from srcScene into this Scene
        std::function<Entity(Entity, Scene*)> cloneRecursive = [&](Entity src, Scene* source) -> Entity {
            std::string name = src.GetComponent<TagComponent>().Tag;
            Entity dst = CreateEntity(name);

            dst.GetComponent<TransformComponent>() = src.GetComponent<TransformComponent>();
            if (src.HasComponent<CameraComponent>()) {
                auto& cam = dst.AddComponent<CameraComponent>(src.GetComponent<CameraComponent>());
                cam.Primary = false;
            }
            CopyComponentIfExists<MeshRendererComponent>(dst, src);
            CopyComponentIfExists<SpriteRendererComponent>(dst, src);
            CopyComponentIfExists<DirectionalLightComponent>(dst, src);
            CopyComponentIfExists<PointLightComponent>(dst, src);
            CopyComponentIfExists<SpotLightComponent>(dst, src);
            CopyComponentIfExists<Rigidbody2DComponent>(dst, src);
            CopyComponentIfExists<BoxCollider2DComponent>(dst, src);
            CopyComponentIfExists<LuaScriptComponent>(dst, src);
            CopyComponentIfExists<PostProcessVolumeComponent>(dst, src);
            CopyComponentIfExists<EnvironmentComponent>(dst, src);
            CopyComponentIfExists<AnimationControllerComponent>(dst, src);

            // Clone children recursively
            auto& srcRel = src.GetComponent<RelationshipComponent>();
            for (auto childID : srcRel.Children) {
                Entity srcChild{ childID, source };
                Entity dstChild = cloneRecursive(srcChild, source);
                dstChild.SetParent(dst, false);
                // Defensive: ensure cache is clean after reparenting
                dstChild.GetComponent<TransformComponent>().ResetCache();
            }

            return dst;
        };

        return cloneRecursive(srcRoot, srcScene);
    }

    // ==========================================
    // 游戏开始：按顺序唤醒所有运行时系统！
    // ==========================================
    void Scene::OnRuntimeStart() {
        // 1. 唤醒物理世界
        OnPhysics2DStart();

        // 2. 唤醒所有的 Lua 脚本，为它们分配独立的沙盒环境
        m_Registry.view<LuaScriptComponent>().each([=](auto entityID, auto& lsc) {
            ScriptEngine::OnCreateEntity({ entityID, this }, this);
        });

        // (未来如果有音频系统、粒子系统，统统加在这里！)
    }

    // ==========================================
    // 游戏结束：按顺序清理所有运行时系统的内存！
    // ==========================================
    void Scene::OnRuntimeStop() {
        // 1. 销毁物理世界
        OnPhysics2DStop();

        // 2. 释放 Lua 运行时的沙盒环境指针
        m_Registry.view<LuaScriptComponent>().each([=](auto entityID, auto& lsc) {
            // 虽然 Scene 销毁时会回收组件，但手动置空指针是一个好习惯
            lsc.RuntimeEnvironment = nullptr; 
        });
    }

    void Scene::OnPhysics2DStart() {
        // 创建物理世界，设置标准的地球重力向下 9.8
        m_PhysicsWorld = new b2World({ 0.0f, -9.8f });

        // 遍历所有带有 Rigidbody2D 的实体
        auto view = m_Registry.view<Rigidbody2DComponent>();
        for (auto e : view) {
            Entity entity = { e, this };
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();

            // 1. 定义物理身体
            b2BodyDef bodyDef;
            bodyDef.type = rb2d.Type == Rigidbody2DComponent::BodyType::Static ? b2_staticBody : 
                           (rb2d.Type == Rigidbody2DComponent::BodyType::Dynamic ? b2_dynamicBody : b2_kinematicBody);
            
            bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
            bodyDef.angle = transform.Rotation.z; // 2D 物理只关心 Z 轴旋转
            bodyDef.fixedRotation = rb2d.FixedRotation;

            // 2. 在物理世界中生成它
            b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
            rb2d.RuntimeBody = body; // 存入组件，供后续使用

            // 3. 如果它也有碰撞体，挂载形状！
            if (entity.HasComponent<BoxCollider2DComponent>()) {
                auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();

                b2PolygonShape boxShape;
                // Box2D 的 SetAsBox 接受半宽/半高。我们需要把 Transform 的 Scale 也乘进去！
                boxShape.SetAsBox(bc2d.Size.x * transform.Scale.x, bc2d.Size.y * transform.Scale.y, b2Vec2(bc2d.Offset.x, bc2d.Offset.y), 0.0f);

                b2FixtureDef fixtureDef;
                fixtureDef.shape = &boxShape;
                fixtureDef.density = bc2d.Density;
                fixtureDef.friction = bc2d.Friction;
                fixtureDef.restitution = bc2d.Restitution;
                fixtureDef.restitutionThreshold = bc2d.RestitutionThreshold;

                // 将形状附加到身体上
                body->CreateFixture(&fixtureDef);
                bc2d.RuntimeFixture = body->GetFixtureList();
            }
        }
    }

    void Scene::OnPhysics2DStop() {
        delete m_PhysicsWorld;
        m_PhysicsWorld = nullptr;
    }

    void Scene::OnUpdateRuntime(Timestep ts) {

        // ==========================================
        // 1. UI 布局计算
        // ==========================================
        UILayoutSystem::Update(*this,
            (uint32_t)Application::Get().GetWindow().GetWidth(),
            (uint32_t)Application::Get().GetWindow().GetHeight());

        // ==========================================
        // 2. 执行 Lua 逻辑
        // ==========================================
        m_Registry.view<LuaScriptComponent>().each([=](auto entityID, auto& lsc) {
            ScriptEngine::OnUpdateEntity({ entityID, this }, ts);
        });

        // ==========================================
        // 2.5. 动画系统 (曲线驱动 + Tween 补间)
        // ==========================================
        m_AnimationTime += ts.GetSeconds();
        AnimationSystem::Update(*this, m_AnimationTime);
        m_TweenManager.Update(ts);

        // ==========================================
        // 3. 物理步进计算
        // ==========================================
        const int32_t velocityIterations = 6;
        const int32_t positionIterations = 2;
        if (m_PhysicsWorld) {
            m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);

            // ==========================================
            // 2. 将计算结果强行覆盖回 Transform！
            // ==========================================
            auto view = m_Registry.view<Rigidbody2DComponent>();
            for (auto e : view) {
                Entity entity = { e, this };
                auto& transform = entity.GetComponent<TransformComponent>();
                auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();

                b2Body* body = (b2Body*)rb2d.RuntimeBody;
                if (body) {
                    const auto& position = body->GetPosition();
                    transform.Translation.x = position.x;
                    transform.Translation.y = position.y;
                    // 同步旋转 (Box2D 计算出的弧度直接赋给 Z 轴)
                    transform.Rotation.z = body->GetAngle();
                }
            }
        }

        // ... 在这里执行你原本的相机抓取和 RenderScene 逻辑 ...
    }

    std::unordered_set<UUID> Scene::GetActiveAssetHandles() const {
        std::unordered_set<UUID> handles;

        // 1. MeshRendererComponent → ModelHandle + MaterialHandle
        auto meshView = m_Registry.view<MeshRendererComponent>();
        for (auto e : meshView) {
            auto& mrc = meshView.get<MeshRendererComponent>(e);
            if (mrc.ModelHandle)    handles.insert(mrc.ModelHandle);
            if (mrc.MaterialHandle) handles.insert(mrc.MaterialHandle);
        }

        // 2. SpriteRendererComponent → TextureHandle
        auto spriteView = m_Registry.view<SpriteRendererComponent>();
        for (auto e : spriteView) {
            auto& src = spriteView.get<SpriteRendererComponent>(e);
            if (src.TextureHandle) handles.insert(src.TextureHandle);
        }

        // 3. EnvironmentComponent → EquirectangularHandle + CubemapHandle
        auto envView = m_Registry.view<EnvironmentComponent>();
        for (auto e : envView) {
            auto& env = envView.get<EnvironmentComponent>(e);
            if (env.EquirectangularHandle) handles.insert(env.EquirectangularHandle);
            if (env.CubemapHandle)        handles.insert(env.CubemapHandle);
        }

        // 4. LuaScriptComponent → ScriptHandle
        auto scriptView = m_Registry.view<LuaScriptComponent>();
        for (auto e : scriptView) {
            auto& lsc = scriptView.get<LuaScriptComponent>(e);
            if (lsc.ScriptHandle) handles.insert(lsc.ScriptHandle);
        }

        // 5. 递归收集材质中引用的贴图 UUID（先拷贝再遍历，防止 insert 使迭代器失效）
        auto& registry = AssetManager::GetRegistry();
        std::vector<UUID> materialHandles;
        for (UUID h : handles) {
            auto it = registry.find(h);
            if (it != registry.end() && it->second.Type == AssetType::Material)
                materialHandles.push_back(h);
        }
        for (UUID matHandle : materialHandles) {
            auto mat = AssetManager::GetAsset<Material>(matHandle);
            if (mat) {
                for (const auto& prop : mat->Properties) {
                    if (prop.TextureHandle) handles.insert(prop.TextureHandle);
                }
            }
        }

        // 6. SubMesh → include parent Model UUID so parent is not GC'd
        {
            std::vector<UUID> subMeshHandles;
            for (UUID h : handles) {
                auto it = registry.find(h);
                if (it != registry.end() && it->second.Type == AssetType::SubMesh
                    && it->second.ParentHandle != 0)
                    subMeshHandles.push_back(h);
            }
            for (UUID smHandle : subMeshHandles) {
                auto it = registry.find(smHandle);
                if (it != registry.end())
                    handles.insert(it->second.ParentHandle);
            }
        }

        return handles;
    }
}