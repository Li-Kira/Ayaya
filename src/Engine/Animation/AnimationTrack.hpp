#pragma once

#include "CurveAsset.hpp"
#include <string>
#include <unordered_map>

namespace Ayaya {

    // ==========================================
    // TargetProperty — all animatable properties
    // ==========================================
    enum class TargetProperty : uint8_t {
        // Transform
        Transform_PositionX, Transform_PositionY, Transform_PositionZ,
        Transform_RotationX, Transform_RotationY, Transform_RotationZ,
        Transform_ScaleX, Transform_ScaleY, Transform_ScaleZ,

        // Sprite
        Sprite_ColorR, Sprite_ColorG, Sprite_ColorB, Sprite_ColorA,

        // Camera
        Camera_FOV, Camera_OrthographicSize,

        // Light
        PointLight_Intensity, PointLight_Radius,
        SpotLight_Intensity, SpotLight_Radius, SpotLight_InnerCone, SpotLight_OuterCone,
        DirLight_Intensity,

        // Material
        Material_Metallic, Material_Roughness,

        // UI
        UI_Opacity,
    };

    inline const char* GetTargetPropertyName(TargetProperty p) {
        static const char* names[] = {
            "Transform_PositionX", "Transform_PositionY", "Transform_PositionZ",
            "Transform_RotationX", "Transform_RotationY", "Transform_RotationZ",
            "Transform_ScaleX", "Transform_ScaleY", "Transform_ScaleZ",
            "Sprite_ColorR", "Sprite_ColorG", "Sprite_ColorB", "Sprite_ColorA",
            "Camera_FOV", "Camera_OrthographicSize",
            "PointLight_Intensity", "PointLight_Radius",
            "SpotLight_Intensity", "SpotLight_Radius", "SpotLight_InnerCone", "SpotLight_OuterCone",
            "DirLight_Intensity",
            "Material_Metallic", "Material_Roughness",
            "UI_Opacity",
        };
        return names[(int)p];
    }

    inline TargetProperty ParseTargetProperty(const std::string& s) {
        static const std::unordered_map<std::string, TargetProperty> map = {
            {"Transform_PositionX", TargetProperty::Transform_PositionX},
            {"Transform_PositionY", TargetProperty::Transform_PositionY},
            {"Transform_PositionZ", TargetProperty::Transform_PositionZ},
            {"Transform_RotationX", TargetProperty::Transform_RotationX},
            {"Transform_RotationY", TargetProperty::Transform_RotationY},
            {"Transform_RotationZ", TargetProperty::Transform_RotationZ},
            {"Transform_ScaleX", TargetProperty::Transform_ScaleX},
            {"Transform_ScaleY", TargetProperty::Transform_ScaleY},
            {"Transform_ScaleZ", TargetProperty::Transform_ScaleZ},
            {"Sprite_ColorR", TargetProperty::Sprite_ColorR},
            {"Sprite_ColorG", TargetProperty::Sprite_ColorG},
            {"Sprite_ColorB", TargetProperty::Sprite_ColorB},
            {"Sprite_ColorA", TargetProperty::Sprite_ColorA},
            {"Camera_FOV", TargetProperty::Camera_FOV},
            {"Camera_OrthographicSize", TargetProperty::Camera_OrthographicSize},
            {"PointLight_Intensity", TargetProperty::PointLight_Intensity},
            {"PointLight_Radius", TargetProperty::PointLight_Radius},
            {"SpotLight_Intensity", TargetProperty::SpotLight_Intensity},
            {"SpotLight_Radius", TargetProperty::SpotLight_Radius},
            {"SpotLight_InnerCone", TargetProperty::SpotLight_InnerCone},
            {"SpotLight_OuterCone", TargetProperty::SpotLight_OuterCone},
            {"DirLight_Intensity", TargetProperty::DirLight_Intensity},
            {"Material_Metallic", TargetProperty::Material_Metallic},
            {"Material_Roughness", TargetProperty::Material_Roughness},
            {"UI_Opacity", TargetProperty::UI_Opacity},
        };
        auto it = map.find(s);
        return (it != map.end()) ? it->second : TargetProperty::Transform_PositionX;
    }

    // ==========================================
    // AnimationTrack — one property-to-curve binding
    // ==========================================
    struct AnimationTrack {
        UUID   CurveHandle = 0;
        TargetProperty Property = TargetProperty::Transform_PositionX;
        float  TimeOffset = 0.0f;
    };

    // Display-friendly strings for UI combos (order MUST match TargetProperty enum)
    static const char* s_TargetPropertyStrings[] = {
        "Transform: Position X", "Transform: Position Y", "Transform: Position Z",
        "Transform: Rotation X", "Transform: Rotation Y", "Transform: Rotation Z",
        "Transform: Scale X", "Transform: Scale Y", "Transform: Scale Z",
        "Sprite: Color R", "Sprite: Color G", "Sprite: Color B", "Sprite: Color A",
        "Camera: FOV", "Camera: Ortho Size",
        "PointLight: Intensity", "PointLight: Radius",
        "SpotLight: Intensity", "SpotLight: Radius", "SpotLight: InnerCone", "SpotLight: OuterCone",
        "DirLight: Intensity",
        "Material: Metallic", "Material: Roughness",
        "UI: Opacity",
    };
    static const int s_TargetPropertyCount = sizeof(s_TargetPropertyStrings) / sizeof(const char*);

} // namespace Ayaya
