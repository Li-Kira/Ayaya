#include "ayapch.h"
#include "SceneSerializer.hpp"
#include "Entity.hpp"
#include "Components.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem>

// =====================================================================
// YAML-CPP 扩展：教 YAML 库如何理解 glm::vec3 和 glm::vec4
// =====================================================================
namespace YAML {
    template<>
    struct convert<glm::vec2> {
        static Node encode(const glm::vec2& rhs) {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            return node;
        }

        static bool decode(const Node& node, glm::vec2& rhs) {
            if (!node.IsSequence() || node.size() != 2) return false;
            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            return true;
        }
    };
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

    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& v) {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
        return out;
    }

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
            out << YAML::Key << "IsActive" << YAML::Value << entity.GetComponent<TagComponent>().IsActive;
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
            out << YAML::Key << "EV100" << YAML::Value << cc.EV100;
            out << YAML::Key << "ClearFlag" << YAML::Value << (int)cc.ClearFlag; 
            out << YAML::Key << "BackgroundColor" << YAML::Value << cc.BackgroundColor;
            
            out << YAML::Key << "Camera" << YAML::BeginMap;
            out << YAML::Key << "ProjectionType" << YAML::Value << (int)cc.Camera.GetProjectionType();
            out << YAML::Key << "PerspectiveFOV" << YAML::Value << cc.Camera.GetPerspectiveFOV();
            out << YAML::Key << "PerspectiveNear" << YAML::Value << cc.Camera.GetPerspectiveNearClip();
            out << YAML::Key << "PerspectiveFar" << YAML::Value << cc.Camera.GetPerspectiveFarClip();
            out << YAML::Key << "OrthographicSize" << YAML::Value << cc.Camera.GetOrthographicSize();
            out << YAML::Key << "OrthographicNear" << YAML::Value << cc.Camera.GetOrthographicNearClip();
            out << YAML::Key << "OrthographicFar" << YAML::Value << cc.Camera.GetOrthographicFarClip();
            out << YAML::EndMap; 

            out << YAML::EndMap; 
        }

        if (entity.HasComponent<SpriteRendererComponent>()) {
            out << YAML::Key << "SpriteRendererComponent";
            out << YAML::BeginMap; 
            auto& src = entity.GetComponent<SpriteRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << src.Color;
            out << YAML::Key << "TextureHandle" << YAML::Value << (uint64_t)src.TextureHandle; // 纯粹的数字
            out << YAML::EndMap; 
        }

        // ==========================================
        // 【暴瘦】：3D 网格组件的保存 (砍掉了几百行处理字符串和材质逻辑的代码)
        // ==========================================
        if (entity.HasComponent<MeshRendererComponent>()) {
            out << YAML::Key << "MeshRendererComponent";
            out << YAML::BeginMap; 
            auto& mrc = entity.GetComponent<MeshRendererComponent>();
            
            out << YAML::Key << "ModelHandle" << YAML::Value << (uint64_t)mrc.ModelHandle;
            out << YAML::Key << "MaterialHandle" << YAML::Value << (uint64_t)mrc.MaterialHandle;
            out << YAML::Key << "CastShadows" << YAML::Value << mrc.CastShadows;
            out << YAML::Key << "ReceiveShadows" << YAML::Value << mrc.ReceiveShadows;
            
            out << YAML::EndMap; 
        }

        if (entity.HasComponent<DirectionalLightComponent>()) {
            out << YAML::Key << "DirectionalLightComponent";
            out << YAML::BeginMap;
            auto& dlc = entity.GetComponent<DirectionalLightComponent>();
            out << YAML::Key << "Color" << YAML::Value << dlc.Color;
            out << YAML::Key << "Illuminance" << YAML::Value << dlc.Illuminance; 
            out << YAML::EndMap;
        }

        if (entity.HasComponent<PointLightComponent>()) {
            out << YAML::Key << "PointLightComponent";
            out << YAML::BeginMap; 
            auto& plc = entity.GetComponent<PointLightComponent>();
            out << YAML::Key << "Color" << YAML::Value << plc.Color;
            out << YAML::Key << "LuminousPower" << YAML::Value << plc.LuminousPower;
            out << YAML::Key << "Radius" << YAML::Value << plc.Radius;
            out << YAML::Key << "Falloff" << YAML::Value << plc.Falloff;
            out << YAML::EndMap; 
        }

        // ==========================================
        // 【暴瘦】：环境天空盒的保存
        // ==========================================
        if (entity.HasComponent<EnvironmentComponent>()) {
            out << YAML::Key << "EnvironmentComponent";
            out << YAML::BeginMap;
            auto& env = entity.GetComponent<EnvironmentComponent>();
            
            out << YAML::Key << "Type" << YAML::Value << (int)env.Type;
            out << YAML::Key << "Intensity" << YAML::Value << env.Intensity;
            out << YAML::Key << "AmbientColor" << YAML::Value << env.AmbientColor;
            out << YAML::Key << "EquirectangularHandle" << YAML::Value << (uint64_t)env.EquirectangularHandle;
            out << YAML::Key << "CubemapHandle" << YAML::Value << (uint64_t)env.CubemapHandle;
            
            out << YAML::EndMap;
        }

        if (entity.HasComponent<PostProcessVolumeComponent>()) {
            out << YAML::Key << "PostProcessVolumeComponent";
            out << YAML::BeginMap; 
            auto& ppv = entity.GetComponent<PostProcessVolumeComponent>();
            out << YAML::Key << "IsGlobal" << YAML::Value << ppv.IsGlobal;
            out << YAML::Key << "ToneMappingType" << YAML::Value << ppv.ToneMappingType;
            out << YAML::Key << "Exposure" << YAML::Value << ppv.Exposure;
            out << YAML::Key << "EnableBloom" << YAML::Value << ppv.EnableBloom;
            out << YAML::Key << "BloomThreshold" << YAML::Value << ppv.BloomThreshold;
            out << YAML::Key << "BloomKnee" << YAML::Value << ppv.BloomKnee;
            out << YAML::Key << "BloomRadius" << YAML::Value << ppv.BloomRadius;
            out << YAML::Key << "BloomIntensity" << YAML::Value << ppv.BloomIntensity;
            out << YAML::Key << "EnableFXAA" << YAML::Value << ppv.EnableFXAA;
            out << YAML::Key << "EnableSSAO" << YAML::Value << ppv.EnableSSAO;
            out << YAML::Key << "SSAORadius" << YAML::Value << ppv.SSAORadius;
            out << YAML::Key << "SSAOBias" << YAML::Value << ppv.SSAOBias;
            out << YAML::EndMap; 
        }

        if (entity.HasComponent<Rigidbody2DComponent>()) {
            out << YAML::Key << "Rigidbody2DComponent";
            out << YAML::BeginMap; 
            auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();
            out << YAML::Key << "BodyType" << YAML::Value << (int)rb2d.Type;
            out << YAML::Key << "FixedRotation" << YAML::Value << rb2d.FixedRotation;
            out << YAML::EndMap; 
        }

        if (entity.HasComponent<BoxCollider2DComponent>()) {
            out << YAML::Key << "BoxCollider2DComponent";
            out << YAML::BeginMap; 
            auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();
            out << YAML::Key << "Offset" << YAML::Value << bc2d.Offset;
            out << YAML::Key << "Size" << YAML::Value << bc2d.Size;
            out << YAML::Key << "Density" << YAML::Value << bc2d.Density;
            out << YAML::Key << "Friction" << YAML::Value << bc2d.Friction;
            out << YAML::Key << "Restitution" << YAML::Value << bc2d.Restitution;
            out << YAML::Key << "RestitutionThreshold" << YAML::Value << bc2d.RestitutionThreshold;
            out << YAML::EndMap; 
        }

        // ==========================================
        // 【暴瘦】：脚本组件的保存
        // ==========================================
        if (entity.HasComponent<LuaScriptComponent>()) {
            out << YAML::Key << "LuaScriptComponent";
            out << YAML::BeginMap;
            auto& lsc = entity.GetComponent<LuaScriptComponent>();
            out << YAML::Key << "ScriptHandle" << YAML::Value << (uint64_t)lsc.ScriptHandle;
            // Persist user-modified CONFIG values
            if (!lsc.ConfigOverrides.empty()) {
                out << YAML::Key << "ConfigOverrides" << YAML::Value << YAML::BeginMap;
                for (auto& kv : lsc.ConfigOverrides)
                    out << YAML::Key << kv.first << YAML::Value << kv.second;
                out << YAML::EndMap;
            }
            out << YAML::EndMap;
        }

        if (entity.HasComponent<RectTransformComponent>()) {
            out << YAML::Key << "RectTransformComponent";
            out << YAML::BeginMap;
            auto& rt = entity.GetComponent<RectTransformComponent>();
            out << YAML::Key << "AnchorMin" << YAML::Value << rt.AnchorMin;
            out << YAML::Key << "AnchorMax" << YAML::Value << rt.AnchorMax;
            out << YAML::Key << "Pivot"     << YAML::Value << rt.Pivot;
            out << YAML::Key << "Position"  << YAML::Value << rt.Position;
            out << YAML::Key << "Size"      << YAML::Value << rt.Size;
            out << YAML::Key << "Rotation"  << YAML::Value << rt.Rotation;
            out << YAML::Key << "Scale"     << YAML::Value << rt.Scale;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<CanvasComponent>()) {
            out << YAML::Key << "CanvasComponent";
            out << YAML::BeginMap;
            auto& cc = entity.GetComponent<CanvasComponent>();
            out << YAML::Key << "Mode"      << YAML::Value << (int)cc.Mode;
            out << YAML::Key << "SortOrder" << YAML::Value << cc.SortOrder;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<UIImageComponent>()) {
            out << YAML::Key << "UIImageComponent";
            out << YAML::BeginMap;
            auto& img = entity.GetComponent<UIImageComponent>();
            out << YAML::Key << "Color"         << YAML::Value << img.Color;
            out << YAML::Key << "TextureHandle" << YAML::Value << (uint64_t)img.TextureHandle;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<UITextComponent>()) {
            out << YAML::Key << "UITextComponent";
            out << YAML::BeginMap;
            auto& txt = entity.GetComponent<UITextComponent>();
            out << YAML::Key << "Text"      << YAML::Value << txt.Text;
            out << YAML::Key << "FontHandle"<< YAML::Value << (uint64_t)txt.FontHandle;
            out << YAML::Key << "Color"     << YAML::Value << txt.Color;
            out << YAML::Key << "FontSize"  << YAML::Value << txt.FontSize;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<UIButtonComponent>()) {
            out << YAML::Key << "UIButtonComponent";
            out << YAML::BeginMap;
            auto& btn = entity.GetComponent<UIButtonComponent>();
            out << YAML::Key << "NormalColor"    << YAML::Value << btn.NormalColor;
            out << YAML::Key << "HoverColor"     << YAML::Value << btn.HoverColor;
            out << YAML::Key << "PressedColor"   << YAML::Value << btn.PressedColor;
            out << YAML::Key << "OnClickCallback"<< YAML::Value << btn.OnClickCallback;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<RelationshipComponent>()) {
            out << YAML::Key << "RelationshipComponent";
            out << YAML::BeginMap;
            auto& rel = entity.GetComponent<RelationshipComponent>();
            
            uint64_t parentUUID = 0;
            if (rel.Parent != entt::null) {
                Entity parentEntity{ rel.Parent, scene }; 
                parentUUID = parentEntity.GetComponent<IDComponent>().ID;
            }
            out << YAML::Key << "Parent" << YAML::Value << parentUUID;
            out << YAML::EndMap;
        }

        out << YAML::EndMap; 
    }

    static void SerializeEntityRecursively(YAML::Emitter& out, Entity entity, Scene* scene) {
        if (!entity) return;
        
        SerializeEntity(out, entity, scene);
        
        if (entity.HasComponent<RelationshipComponent>()) {
            auto& rel = entity.GetComponent<RelationshipComponent>();
            for (auto childID : rel.Children) {
                Entity child = { childID, scene };
                SerializeEntityRecursively(out, child, scene);
            }
        }
    }

    void SceneSerializer::Serialize(const std::string& filepath, const EditorState& editorState) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << "Untitled";
        
        out << YAML::Key << "EditorState" << YAML::BeginMap;
        editorState.ForEach([&](const char* key, const auto& value) {
            out << YAML::Key << key << YAML::Value << value;
        });
        out << YAML::EndMap;

        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

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
    bool SceneSerializer::Deserialize(const std::string& filepath, EditorState& outEditorState, std::function<void(float, const std::string&)> progressCallback) {

        YAML::Node data;
        try {
            data = YAML::LoadFile(filepath);
        } catch (YAML::ParserException e) {
            AYAYA_CORE_ERROR("Failed to load .ayaya file '{0}'\n     {1}", filepath, e.what());
            return false;
        }

        if (!data["Scene"]) return false;

        outEditorState = EditorState(); 
        auto editorStateNode = data["EditorState"];
        
        if (editorStateNode) {
            outEditorState.ForEach([&](const char* key, auto& value) {
                if (editorStateNode[key]) {
                    using FieldType = std::decay_t<decltype(value)>;
                    value = editorStateNode[key].as<FieldType>();
                }
            });
        }

        std::string sceneName = data["Scene"].as<std::string>();
        AYAYA_CORE_INFO("Deserializing scene '{0}'", sceneName);

        auto entities = data["Entities"];
        if (!entities) return true;

        int totalEntities = entities.size();
        int currentEntityIndex = 0;

        std::unordered_map<uint64_t, Entity> sceneEntities;
        
        struct EntityRel {
            Entity ChildEntity;
            uint64_t ParentUUID;
        };
        std::vector<EntityRel> relationshipsToResolve;

        for (auto entity : entities) {
            uint64_t uuid = entity["Entity"].as<uint64_t>();

            std::string name;
            bool isActive = true;
            auto tagComponent = entity["TagComponent"];
            if (tagComponent) {
                name = tagComponent["Tag"].as<std::string>();
                if (tagComponent["IsActive"]) {
                    isActive = tagComponent["IsActive"].as<bool>();
                }
            }

            if (progressCallback) {
                float progress = (float)currentEntityIndex / (float)totalEntities;
                progressCallback(progress, "Spawning Entity: " + name);
            }

            AYAYA_CORE_TRACE("Deserialized entity with ID = {0}, name = {1}", uuid, name);

            Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);
            sceneEntities[uuid] = deserializedEntity;
            deserializedEntity.GetComponent<TagComponent>().IsActive = isActive;

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

                if (cameraComponent["EV100"]) cc.EV100 = cameraComponent["EV100"].as<float>();
                if (cameraComponent["ClearFlag"]) cc.ClearFlag = (CameraComponent::ClearFlags)cameraComponent["ClearFlag"].as<int>();
                if (cameraComponent["BackgroundColor"]) cc.BackgroundColor = cameraComponent["BackgroundColor"].as<glm::vec4>();

                auto cameraProps = cameraComponent["Camera"];
                if (cameraProps) {
                    cc.Camera.SetProjectionType((SceneCamera::ProjectionType)cameraProps["ProjectionType"].as<int>());
                    cc.Camera.SetPerspectiveFOV(cameraProps["PerspectiveFOV"].as<float>());
                    cc.Camera.SetPerspectiveNearClip(cameraProps["PerspectiveNear"].as<float>());
                    cc.Camera.SetPerspectiveFarClip(cameraProps["PerspectiveFar"].as<float>());
                    cc.Camera.SetOrthographicSize(cameraProps["OrthographicSize"].as<float>());
                    cc.Camera.SetOrthographicNearClip(cameraProps["OrthographicNear"].as<float>());
                    cc.Camera.SetOrthographicFarClip(cameraProps["OrthographicFar"].as<float>());
                }
            }

            auto spriteRendererComponent = entity["SpriteRendererComponent"];
            if (spriteRendererComponent) {
                auto& src = deserializedEntity.AddComponent<SpriteRendererComponent>();
                src.Color = spriteRendererComponent["Color"].as<glm::vec4>();
                if (spriteRendererComponent["TextureHandle"]) { 
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
            // 【暴瘦】：直接读取 Handle 给 MeshRenderer！不再当场 Load！
            // ==========================================
            auto meshRendererComponent = entity["MeshRendererComponent"];
            if (meshRendererComponent) {
                auto& mrc = deserializedEntity.AddComponent<MeshRendererComponent>();
                
                if (meshRendererComponent["ModelHandle"]) mrc.ModelHandle = meshRendererComponent["ModelHandle"].as<uint64_t>();
                if (meshRendererComponent["MaterialHandle"]) mrc.MaterialHandle = meshRendererComponent["MaterialHandle"].as<uint64_t>();

                mrc.CastShadows = meshRendererComponent["CastShadows"] ? meshRendererComponent["CastShadows"].as<bool>() : false; 
                mrc.ReceiveShadows = meshRendererComponent["ReceiveShadows"] ? meshRendererComponent["ReceiveShadows"].as<bool>() : false;
            }

            auto dirLightComponent = entity["DirectionalLightComponent"];
            if (dirLightComponent) {
                auto& dlc = deserializedEntity.AddComponent<DirectionalLightComponent>();
                dlc.Color = dirLightComponent["Color"].as<glm::vec3>();

                if (dirLightComponent["Illuminance"]) dlc.Illuminance = dirLightComponent["Illuminance"].as<float>();
                else dlc.Illuminance = 100000.0f;
            }

            auto pointLightComponent = entity["PointLightComponent"];
            if (pointLightComponent) {
                auto& plc = deserializedEntity.AddComponent<PointLightComponent>();
                if (pointLightComponent["Color"]) plc.Color = pointLightComponent["Color"].as<glm::vec3>();
                if (pointLightComponent["LuminousPower"]) plc.LuminousPower = pointLightComponent["LuminousPower"].as<float>();
                if (pointLightComponent["Radius"]) plc.Radius = pointLightComponent["Radius"].as<float>();
                if (pointLightComponent["Falloff"]) plc.Falloff = pointLightComponent["Falloff"].as<float>();
            }

            // ==========================================
            // 【暴瘦】：环境天空盒直接读取 Handle
            // ==========================================
            auto envComponent = entity["EnvironmentComponent"];
            if (envComponent) {
                auto& env = deserializedEntity.AddComponent<EnvironmentComponent>();
                
                env.Type = (EnvironmentType)envComponent["Type"].as<int>();
                env.Intensity = envComponent["Intensity"].as<float>();
                if (envComponent["AmbientColor"]) env.AmbientColor = envComponent["AmbientColor"].as<glm::vec3>();

                if (envComponent["EquirectangularHandle"]) env.EquirectangularHandle = envComponent["EquirectangularHandle"].as<uint64_t>();
                if (envComponent["CubemapHandle"]) env.CubemapHandle = envComponent["CubemapHandle"].as<uint64_t>();
                
                env.IsDirty = true; // 告诉渲染器这玩意需要加载
            }

            auto ppvComponent = entity["PostProcessVolumeComponent"];
            if (ppvComponent) {
                auto& ppv = deserializedEntity.AddComponent<PostProcessVolumeComponent>();
                ppv.IsGlobal = ppvComponent["IsGlobal"].as<bool>();
                ppv.ToneMappingType = ppvComponent["ToneMappingType"].as<int>();
                ppv.Exposure = ppvComponent["Exposure"].as<float>();
                ppv.EnableBloom = ppvComponent["EnableBloom"].as<bool>();
                ppv.BloomThreshold = ppvComponent["BloomThreshold"].as<float>();
                ppv.BloomKnee = ppvComponent["BloomKnee"].as<float>();
                ppv.BloomRadius = ppvComponent["BloomRadius"].as<float>();
                ppv.BloomIntensity = ppvComponent["BloomIntensity"].as<float>();
                ppv.EnableFXAA = ppvComponent["EnableFXAA"].as<bool>();
                if (ppvComponent["EnableSSAO"]) ppv.EnableSSAO = ppvComponent["EnableSSAO"].as<bool>();
                if (ppvComponent["SSAORadius"]) ppv.SSAORadius = ppvComponent["SSAORadius"].as<float>();
                if (ppvComponent["SSAOBias"])   ppv.SSAOBias   = ppvComponent["SSAOBias"].as<float>();
            }

            auto rigidbody2DComponent = entity["Rigidbody2DComponent"];
            if (rigidbody2DComponent) {
                auto& rb2d = deserializedEntity.AddComponent<Rigidbody2DComponent>();
                rb2d.Type = (Rigidbody2DComponent::BodyType)rigidbody2DComponent["BodyType"].as<int>();
                rb2d.FixedRotation = rigidbody2DComponent["FixedRotation"].as<bool>();
            }

            auto boxCollider2DComponent = entity["BoxCollider2DComponent"];
            if (boxCollider2DComponent) {
                auto& bc2d = deserializedEntity.AddComponent<BoxCollider2DComponent>();
                bc2d.Offset = boxCollider2DComponent["Offset"].as<glm::vec2>();
                bc2d.Size = boxCollider2DComponent["Size"].as<glm::vec2>();
                bc2d.Density = boxCollider2DComponent["Density"].as<float>();
                bc2d.Friction = boxCollider2DComponent["Friction"].as<float>();
                bc2d.Restitution = boxCollider2DComponent["Restitution"].as<float>();
                bc2d.RestitutionThreshold = boxCollider2DComponent["RestitutionThreshold"].as<float>();
            }

            // ==========================================
            // 【暴瘦】：Lua 组件只读取 Handle
            // ==========================================
            auto luaScriptComponent = entity["LuaScriptComponent"];
            if (luaScriptComponent) {
                auto& lsc = deserializedEntity.AddComponent<LuaScriptComponent>();
                if (luaScriptComponent["ScriptHandle"]) {
                    lsc.ScriptHandle = luaScriptComponent["ScriptHandle"].as<uint64_t>();
                }
                // Restore user-modified CONFIG values
                auto overrides = luaScriptComponent["ConfigOverrides"];
                if (overrides && overrides.IsMap()) {
                    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
                        std::string key = it->first.as<std::string>();
                        std::string val = it->second.as<std::string>();
                        lsc.ConfigOverrides[key] = val;
                    }
                }
            }

            auto rectTransform = entity["RectTransformComponent"];
            if (rectTransform) {
                auto& rt = deserializedEntity.AddComponent<RectTransformComponent>();
                if (rectTransform["AnchorMin"]) rt.AnchorMin = rectTransform["AnchorMin"].as<glm::vec2>();
                if (rectTransform["AnchorMax"]) rt.AnchorMax = rectTransform["AnchorMax"].as<glm::vec2>();
                if (rectTransform["Pivot"])     rt.Pivot     = rectTransform["Pivot"].as<glm::vec2>();
                if (rectTransform["Position"])  rt.Position  = rectTransform["Position"].as<glm::vec2>();
                if (rectTransform["Size"])      rt.Size      = rectTransform["Size"].as<glm::vec2>();
                if (rectTransform["Rotation"])  rt.Rotation  = rectTransform["Rotation"].as<float>();
                if (rectTransform["Scale"])     rt.Scale     = rectTransform["Scale"].as<glm::vec2>();
            }

            auto canvasComponent = entity["CanvasComponent"];
            if (canvasComponent) {
                auto& cc = deserializedEntity.AddComponent<CanvasComponent>();
                if (canvasComponent["Mode"])      cc.Mode      = (CanvasComponent::RenderMode)canvasComponent["Mode"].as<int>();
                if (canvasComponent["SortOrder"]) cc.SortOrder = canvasComponent["SortOrder"].as<int>();
            }

            auto uiImage = entity["UIImageComponent"];
            if (uiImage) {
                auto& img = deserializedEntity.AddComponent<UIImageComponent>();
                if (uiImage["Color"])         img.Color         = uiImage["Color"].as<glm::vec4>();
                if (uiImage["TextureHandle"]) img.TextureHandle = uiImage["TextureHandle"].as<uint64_t>();
            }

            auto uiText = entity["UITextComponent"];
            if (uiText) {
                auto& txt = deserializedEntity.AddComponent<UITextComponent>();
                if (uiText["Text"])      txt.Text      = uiText["Text"].as<std::string>();
                if (uiText["FontHandle"]) txt.FontHandle = uiText["FontHandle"].as<uint64_t>();
                if (uiText["Color"])     txt.Color     = uiText["Color"].as<glm::vec4>();
                if (uiText["FontSize"])  txt.FontSize  = uiText["FontSize"].as<float>();
            }

            auto uiButton = entity["UIButtonComponent"];
            if (uiButton) {
                auto& btn = deserializedEntity.AddComponent<UIButtonComponent>();
                if (uiButton["NormalColor"])     btn.NormalColor     = uiButton["NormalColor"].as<glm::vec4>();
                if (uiButton["HoverColor"])      btn.HoverColor      = uiButton["HoverColor"].as<glm::vec4>();
                if (uiButton["PressedColor"])    btn.PressedColor    = uiButton["PressedColor"].as<glm::vec4>();
                if (uiButton["OnClickCallback"]) btn.OnClickCallback = uiButton["OnClickCallback"].as<std::string>();
            }

            currentEntityIndex++;
        }

        if (progressCallback) progressCallback(1.0f, "Resolving Relationships...");
        
        for (auto& rel : relationshipsToResolve) {
            if (sceneEntities.find(rel.ParentUUID) != sceneEntities.end()) {
                Entity parentEntity = sceneEntities[rel.ParentUUID];
                rel.ChildEntity.SetParent(parentEntity, false); 
            }
        }

        return true;
    }
}