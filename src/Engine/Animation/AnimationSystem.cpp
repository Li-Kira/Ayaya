#include "AnimationSystem.hpp"
#include "AnimationTrack.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Components.hpp"
#include "Asset/AssetManager.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    void AnimationSystem::Update(Scene& scene, float currentTime) {
        auto view = scene.Reg().view<AnimationControllerComponent>();
        for (auto entityID : view) {
            Entity entity{ entityID, &scene };
            auto& controller = entity.GetComponent<AnimationControllerComponent>();
            if (!controller.IsPlaying) continue;

            for (auto& track : controller.Tracks) {
                if (track.CurveHandle == 0) continue;

                float localTime = currentTime - track.TimeOffset;

                auto curve = AssetManager::GetAsset<CurveAsset>(track.CurveHandle);
                if (!curve || curve->Keys.empty()) continue;

                float value = curve->Evaluate(localTime);

                switch (track.Property) {
                // ---- Transform ----
                case TargetProperty::Transform_PositionX:
                    entity.GetComponent<TransformComponent>().Translation.x = value; break;
                case TargetProperty::Transform_PositionY:
                    entity.GetComponent<TransformComponent>().Translation.y = value; break;
                case TargetProperty::Transform_PositionZ:
                    entity.GetComponent<TransformComponent>().Translation.z = value; break;
                case TargetProperty::Transform_RotationX:
                    entity.GetComponent<TransformComponent>().Rotation.x = value; break;
                case TargetProperty::Transform_RotationY:
                    entity.GetComponent<TransformComponent>().Rotation.y = value; break;
                case TargetProperty::Transform_RotationZ:
                    entity.GetComponent<TransformComponent>().Rotation.z = value; break;
                case TargetProperty::Transform_ScaleX:
                    entity.GetComponent<TransformComponent>().Scale.x = value; break;
                case TargetProperty::Transform_ScaleY:
                    entity.GetComponent<TransformComponent>().Scale.y = value; break;
                case TargetProperty::Transform_ScaleZ:
                    entity.GetComponent<TransformComponent>().Scale.z = value; break;

                // ---- Sprite ----
                case TargetProperty::Sprite_ColorR:
                    if (entity.HasComponent<SpriteRendererComponent>())
                        entity.GetComponent<SpriteRendererComponent>().Color.r = value;
                    break;
                case TargetProperty::Sprite_ColorG:
                    if (entity.HasComponent<SpriteRendererComponent>())
                        entity.GetComponent<SpriteRendererComponent>().Color.g = value;
                    break;
                case TargetProperty::Sprite_ColorB:
                    if (entity.HasComponent<SpriteRendererComponent>())
                        entity.GetComponent<SpriteRendererComponent>().Color.b = value;
                    break;
                case TargetProperty::Sprite_ColorA:
                    if (entity.HasComponent<SpriteRendererComponent>())
                        entity.GetComponent<SpriteRendererComponent>().Color.a = value;
                    break;

                // ---- Camera ----
                case TargetProperty::Camera_FOV:
                    if (entity.HasComponent<CameraComponent>())
                        entity.GetComponent<CameraComponent>().Camera.SetPerspectiveFOV(value);
                    break;
                case TargetProperty::Camera_OrthographicSize:
                    if (entity.HasComponent<CameraComponent>())
                        entity.GetComponent<CameraComponent>().Camera.SetOrthographicSize(value);
                    break;

                // ---- Lights ----
                case TargetProperty::PointLight_Intensity:
                    if (entity.HasComponent<PointLightComponent>())
                        entity.GetComponent<PointLightComponent>().LuminousPower = value;
                    break;
                case TargetProperty::PointLight_Radius:
                    if (entity.HasComponent<PointLightComponent>())
                        entity.GetComponent<PointLightComponent>().Radius = value;
                    break;
                case TargetProperty::DirLight_Intensity:
                    if (entity.HasComponent<DirectionalLightComponent>())
                        entity.GetComponent<DirectionalLightComponent>().Illuminance = value;
                    break;

                case TargetProperty::SpotLight_Intensity:
                    if (entity.HasComponent<SpotLightComponent>())
                        entity.GetComponent<SpotLightComponent>().LuminousPower = value;
                    break;
                case TargetProperty::SpotLight_Radius:
                    if (entity.HasComponent<SpotLightComponent>())
                        entity.GetComponent<SpotLightComponent>().Radius = value;
                    break;
                case TargetProperty::SpotLight_InnerCone:
                    if (entity.HasComponent<SpotLightComponent>())
                        entity.GetComponent<SpotLightComponent>().InnerConeAngle = value;
                    break;
                case TargetProperty::SpotLight_OuterCone:
                    if (entity.HasComponent<SpotLightComponent>())
                        entity.GetComponent<SpotLightComponent>().OuterConeAngle = value;
                    break;

                // ---- Material (stub — requires material API) ----
                case TargetProperty::Material_Metallic:
                case TargetProperty::Material_Roughness:
                    // Material properties live on the Material asset, not an ECS component.
                    // Future: look up via entity->MeshRendererComponent->MaterialHandle,
                    // then modify material->SetProperty("Metallic", value).
                    break;

                // ---- UI ----
                case TargetProperty::UI_Opacity:
                    // Apply to all UI renderer components on this entity
                    if (entity.HasComponent<UIImageComponent>()) {
                        auto& img = entity.GetComponent<UIImageComponent>();
                        img.Color.a = glm::clamp(value, 0.0f, 1.0f);
                    }
                    if (entity.HasComponent<UITextComponent>()) {
                        auto& txt = entity.GetComponent<UITextComponent>();
                        txt.Color.a = glm::clamp(value, 0.0f, 1.0f);
                    }
                    break;
                }
            }
        }
    }

} // namespace Ayaya
