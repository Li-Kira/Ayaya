#include "ayapch.h"
#include "SceneSerializer.hpp"
#include "Entity.hpp"
#include "Components.hpp"
#include "Renderer/MaterialSerializer.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>

// =====================================================================
// YAML-CPP 扩展：教 YAML 库如何理解 glm::vec3 和 glm::vec4
// =====================================================================
namespace YAML {
    template<>
    struct convert<glm::vec3> {
        static Node encode(const glm::vec3& rhs) {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            return node;
        }

        static bool decode(const Node& node, glm::vec3& rhs) {
            if (!node.IsSequence() || node.size() != 3) return false;
            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            return true;
        }
    };

    template<>
    struct convert<glm::vec4> {
        static Node encode(const glm::vec4& rhs) {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            node.push_back(rhs.w);
            return node;
        }

        static bool decode(const Node& node, glm::vec4& rhs) {
            if (!node.IsSequence() || node.size() != 4) return false;
            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            rhs.w = node[3].as<float>();
            return true;
        }
    };
}

namespace Ayaya {

    // 辅助重载：让 YAML::Emitter 支持直接流式输出 glm::vec3
    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v) {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
        return out;
    }
    
    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v) {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
        return out;
    }

    SceneSerializer::SceneSerializer(const std::shared_ptr<Scene>& scene)
        : m_Scene(scene) {
    }

    // =====================================================================
    // 核心逻辑：序列化 (保存)
    // =====================================================================
    static void SerializeEntity(YAML::Emitter& out, Entity entity, Scene* scene) {
        out << YAML::BeginMap; // Entity Map
        out << YAML::Key << "Entity" << YAML::Value << (uint64_t)entity.GetComponent<IDComponent>().ID;

        if (entity.HasComponent<TagComponent>()) {
            out << YAML::Key << "TagComponent";
            out << YAML::BeginMap;
            out << YAML::Key << "Tag" << YAML::Value << entity.GetComponent<TagComponent>().Tag;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<TransformComponent>()) {
            out << YAML::Key << "TransformComponent";
            out << YAML::BeginMap;
            auto& tc = entity.GetComponent<TransformComponent>();
            out << YAML::Key << "Translation" << YAML::Value << tc.Translation;
            out << YAML::Key << "Rotation" << YAML::Value << tc.Rotation;
            out << YAML::Key << "Scale" << YAML::Value << tc.Scale;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<CameraComponent>()) {
            out << YAML::Key << "CameraComponent";
            out << YAML::BeginMap;
            auto& cc = entity.GetComponent<CameraComponent>();
            out << YAML::Key << "Primary" << YAML::Value << cc.Primary;
            out << YAML::Key << "FixedAspectRatio" << YAML::Value << cc.FixedAspectRatio;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<SpriteRendererComponent>()) {
            out << YAML::Key << "SpriteRendererComponent";
            out << YAML::BeginMap; // 开始组件 Map
            
            auto& src = entity.GetComponent<SpriteRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << src.Color;
            out << YAML::Key << "TextureHandle" << YAML::Value << (uint64_t)src.TextureHandle;
            
            out << YAML::EndMap; // 结束组件 Map
        }

        // ==========================================
        // 保存 3D 网格组件
        // ==========================================
        if (entity.HasComponent<MeshRendererComponent>()) {
            out << YAML::Key << "MeshRendererComponent";
            out << YAML::BeginMap; 
            
            auto& mrc = entity.GetComponent<MeshRendererComponent>();
            
            // 1. 记录模型路径
            if (mrc.ModelAsset && !mrc.ModelAsset->GetPath().empty()) {
                out << YAML::Key << "ModelPath" << YAML::Value << mrc.ModelAsset->GetPath();
            }

            // ==========================================
            // 2. 核心：在保存场景时，级联保存材质！
            // ==========================================
            if (mrc.MaterialAsset) {
                auto& mat = mrc.MaterialAsset;
                
                // 检查：如果这是一个没有路径的游离材质，或者是编辑器内置的只读模板
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
                    
                    // 查重并追加序号
                    while (std::filesystem::exists(finalPath)) {
                        finalPath = "assets/materials/" + baseName + " (" + std::to_string(index) + ").mat";
                        index++;
                    }
                    
                    mat->AssetPath = finalPath;
                    mat->Name = std::filesystem::path(finalPath).stem().string();
                    
                    // 落地保存为新文件
                    MaterialSerializer::Serialize(mat, mat->AssetPath);
                    AYAYA_CORE_INFO("Auto-saved new material to {0}", mat->AssetPath);
                } 
                else {
                    // 如果它已经是一个存在的用户材质，顺手覆盖保存它的最新参数！
                    // 这样就算用户在面板上调了颜色忘记点 Save，保存场景时也会一并把材质文件更新
                    MaterialSerializer::Serialize(mat, mat->AssetPath);
                }

                // 3. 将最终确定的合法路径写入场景文件
                out << YAML::Key << "MaterialPath" << YAML::Value << mat->AssetPath;
            }
            
            out << YAML::EndMap; 
        }

        if (entity.HasComponent<DirectionalLightComponent>()) {
            out << YAML::Key << "DirectionalLightComponent";
            out << YAML::BeginMap;
            auto& dlc = entity.GetComponent<DirectionalLightComponent>();
            out << YAML::Key << "Color" << YAML::Value << dlc.Color;
            out << YAML::Key << "AmbientStrength" << YAML::Value << dlc.AmbientStrength;
            out << YAML::EndMap;
        }

        // ==========================================
        // 新增：序列化 PointLightComponent
        // ==========================================
        if (entity.HasComponent<PointLightComponent>()) {
            out << YAML::Key << "PointLightComponent";
            out << YAML::BeginMap; // PointLightComponent
            
            auto& plc = entity.GetComponent<PointLightComponent>();
            out << YAML::Key << "Color" << YAML::Value << plc.Color;
            out << YAML::Key << "Intensity" << YAML::Value << plc.Intensity;
            
            out << YAML::EndMap; // PointLightComponent
        }

        // --- 核心：保存父子层级关系 UUID ---
        if (entity.HasComponent<RelationshipComponent>()) {
            out << YAML::Key << "RelationshipComponent";
            out << YAML::BeginMap;
            auto& rel = entity.GetComponent<RelationshipComponent>();
            
            uint64_t parentUUID = 0;
            if (rel.Parent != entt::null) {
                // 修复：使用传进来的 scene 指针，而不是违规访问私有变量
                Entity parentEntity{ rel.Parent, scene }; 
                parentUUID = parentEntity.GetComponent<IDComponent>().ID;
            }
            out << YAML::Key << "Parent" << YAML::Value << parentUUID;
            out << YAML::EndMap;
        }

        out << YAML::EndMap; // End Entity Map
    }

    // --- 新增：深度优先递归保存 ---
    // --- 新增：深度优先递归保存 ---
    static void SerializeEntityRecursively(YAML::Emitter& out, Entity entity, Scene* scene) {
        if (!entity) return;
        
        // 1. 先保存自己
        SerializeEntity(out, entity, scene);
        
        // 2. 按照顺序递归保存所有的子节点
        if (entity.HasComponent<RelationshipComponent>()) {
            auto& rel = entity.GetComponent<RelationshipComponent>();
            // 遍历所有子节点的 ID，递归调用
            for (auto childID : rel.Children) {
                Entity child = { childID, scene };
                SerializeEntityRecursively(out, child, scene);
            }
        }
    }

    void SceneSerializer::Serialize(const std::string& filepath) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << "Untitled";
        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        // =======================================================
        // 【核心修复】：不再使用无序的 EnTT view，
        // 而是严格按照大纲树的真实显示顺序进行深度优先遍历！
        // =======================================================
        auto rootEntities = m_Scene->GetRootEntities();
        for (auto entityID : rootEntities) {
            Entity entity = { entityID, m_Scene.get() };
            SerializeEntityRecursively(out, entity, m_Scene.get());
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;

        std::ofstream fout(filepath);
        fout << out.c_str();
    }

    // =====================================================================
    // 核心逻辑：反序列化 (读取)
    // =====================================================================
    bool SceneSerializer::Deserialize(const std::string& filepath) {
        YAML::Node data;
        try {
            data = YAML::LoadFile(filepath);
        } catch (YAML::ParserException e) {
            AYAYA_CORE_ERROR("Failed to load .ayaya file '{0}'\n     {1}", filepath, e.what());
            return false;
        }

        if (!data["Scene"]) return false;

        std::string sceneName = data["Scene"].as<std::string>();
        AYAYA_CORE_INFO("Deserializing scene '{0}'", sceneName);

        auto entities = data["Entities"];
        if (!entities) return true;

        // 建立一张表：UUID -> 刚刚在内存里创建的 EnTT Entity
        std::unordered_map<uint64_t, Entity> sceneEntities;
        
        // 记录一下每个物体读取出来的父节点 UUID
        struct EntityRel {
            Entity ChildEntity;
            uint64_t ParentUUID;
        };
        std::vector<EntityRel> relationshipsToResolve;

        // --- 第一遍：纯粹创建实体和基础组件 ---
        for (auto entity : entities) {
            uint64_t uuid = entity["Entity"].as<uint64_t>();

            std::string name;
            auto tagComponent = entity["TagComponent"];
            if (tagComponent) name = tagComponent["Tag"].as<std::string>();

            AYAYA_CORE_TRACE("Deserialized entity with ID = {0}, name = {1}", uuid, name);

            Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);
            sceneEntities[uuid] = deserializedEntity;

            auto transformComponent = entity["TransformComponent"];
            if (transformComponent) {
                auto& tc = deserializedEntity.GetComponent<TransformComponent>();
                tc.Translation = transformComponent["Translation"].as<glm::vec3>();
                tc.Rotation = transformComponent["Rotation"].as<glm::vec3>();
                tc.Scale = transformComponent["Scale"].as<glm::vec3>();
            }

            auto cameraComponent = entity["CameraComponent"];
            if (cameraComponent) {
                auto& cc = deserializedEntity.AddComponent<CameraComponent>();
                cc.Primary = cameraComponent["Primary"].as<bool>();
                cc.FixedAspectRatio = cameraComponent["FixedAspectRatio"].as<bool>();
            }

            auto spriteRendererComponent = entity["SpriteRendererComponent"];
            if (spriteRendererComponent) {
                auto& src = deserializedEntity.AddComponent<SpriteRendererComponent>();
                src.Color = spriteRendererComponent["Color"].as<glm::vec4>();
                if (spriteRendererComponent["TextureHandle"]) { // <-- 新增
                    src.TextureHandle = spriteRendererComponent["TextureHandle"].as<uint64_t>();
                }
            }

            auto relationshipComponent = entity["RelationshipComponent"];
            if (relationshipComponent) {
                uint64_t parentUUID = relationshipComponent["Parent"].as<uint64_t>();
                if (parentUUID != 0) {
                    relationshipsToResolve.push_back({ deserializedEntity, parentUUID });
                }
            }

            // ==========================================
            // 读取 3D 网格组件
            // ==========================================
            auto meshRendererComponent = entity["MeshRendererComponent"];
            if (meshRendererComponent) {
                auto& mrc = deserializedEntity.AddComponent<MeshRendererComponent>();
                
                // 1. 读取并加载模型
                if (meshRendererComponent["ModelPath"]) {
                    std::string modelPath = meshRendererComponent["ModelPath"].as<std::string>();
                    mrc.ModelAsset = std::make_shared<Model>(modelPath);
                }

                // 2. 读取并加载材质资产
                if (meshRendererComponent["MaterialPath"]) {
                    std::string matPath = meshRendererComponent["MaterialPath"].as<std::string>();
                    
                    mrc.MaterialAsset = std::make_shared<Material>();
                    
                    // ==========================================
                    // 核心修复：如果材质文件读取失败，直接将指针置空！
                    // ==========================================
                    bool success = MaterialSerializer::Deserialize(mrc.MaterialAsset, matPath);
                    if (!success) {
                        AYAYA_CORE_WARN("Material file '{0}' is missing! Assigning Fallback Material.", matPath);
                        // 置空指针。SceneRenderer 看到 nullptr 会自动使用品红色的 Fallback Shader
                        mrc.MaterialAsset = nullptr; 
                    }
                }
            }

            auto dirLightComponent = entity["DirectionalLightComponent"];
            if (dirLightComponent) {
                auto& dlc = deserializedEntity.AddComponent<DirectionalLightComponent>();
                dlc.Color = dirLightComponent["Color"].as<glm::vec3>();
                dlc.AmbientStrength = dirLightComponent["AmbientStrength"].as<float>();
            }

            // ==========================================
            // 新增：反序列化 PointLightComponent
            // ==========================================
            auto pointLightComponent = entity["PointLightComponent"];
            if (pointLightComponent) {
                auto& plc = deserializedEntity.AddComponent<PointLightComponent>();
                
                plc.Color = pointLightComponent["Color"].as<glm::vec3>();
                plc.Intensity = pointLightComponent["Intensity"].as<float>();
            }
        }

        // --- 第二遍：完美复原父子层级！ ---
        for (auto& rel : relationshipsToResolve) {
            if (sceneEntities.find(rel.ParentUUID) != sceneEntities.end()) {
                Entity parentEntity = sceneEntities[rel.ParentUUID];
                // 使用 false，表示完全按照文件里存的 localTransform 建立关系，不需要补偿矩阵
                rel.ChildEntity.SetParent(parentEntity, false); 
            }
        }

        return true;
    }
}