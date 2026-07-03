#include "ayapch.h"
#include "MaterialSerializer.hpp"
#include "Asset/AssetManager.hpp"
#include "Core/VFS.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <algorithm>

namespace Ayaya {
    void MaterialSerializer::Serialize(const std::shared_ptr<Material>& material, const std::string& filepath) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "MaterialName" << YAML::Value << material->Name;
        out << YAML::Key << "ShaderName" << YAML::Value << material->ShaderName;
        out << YAML::Key << "BlendMode" << YAML::Value << static_cast<int>(material->GetBlendMode());
        out << YAML::Key << "AlphaCutoff" << YAML::Value << material->GetAlphaCutoff();
        out << YAML::Key << "Packing" << YAML::Value << (uint32_t)material->GetBakedPC().Packing;
        if (!material->GetLightModeStr().empty())
            out << YAML::Key << "LightModes" << YAML::Value << material->GetLightModeStr();

        out << YAML::Key << "Properties" << YAML::BeginSeq;
        for (auto& prop : material->Properties) {
            out << YAML::BeginMap;
            out << YAML::Key << "UniformName" << YAML::Value << prop.UniformName;
            out << YAML::Key << "DisplayName" << YAML::Value << prop.DisplayName;
            out << YAML::Key << "Type" << YAML::Value << (int)prop.Type;

            if (prop.Type == MaterialPropertyType::Float) 
                out << YAML::Key << "FloatValue" << YAML::Value << prop.FloatValue;
            if (prop.Type == MaterialPropertyType::Int) 
                out << YAML::Key << "IntValue" << YAML::Value << prop.IntValue;
            if (prop.Type == MaterialPropertyType::Bool) 
                out << YAML::Key << "BoolValue" << YAML::Value << prop.BoolValue;

            if (prop.Type == MaterialPropertyType::Vec2) {
                out << YAML::Key << "Vec2Value" << YAML::Flow << YAML::BeginSeq 
                    << prop.Vec2Value.x << prop.Vec2Value.y << YAML::EndSeq;
            }

            if (prop.Type == MaterialPropertyType::Vec3) {
                out << YAML::Key << "Vec3Value" << YAML::Flow << YAML::BeginSeq 
                    << prop.Vec3Value.x << prop.Vec3Value.y << prop.Vec3Value.z << YAML::EndSeq;
            }
            
            if (prop.Type == MaterialPropertyType::Vec4) {
                out << YAML::Key << "Vec4Value" << YAML::Flow << YAML::BeginSeq 
                    << prop.Vec4Value.x << prop.Vec4Value.y << prop.Vec4Value.z << prop.Vec4Value.w << YAML::EndSeq;
            }
            
            // ==========================================
            // 【核心修复 1】：保存时直接写入 64 位的 TextureHandle
            // ==========================================
            if (prop.Type == MaterialPropertyType::Texture2D || prop.Type == MaterialPropertyType::TextureCube) {
                out << YAML::Key << "TextureHandle" << YAML::Value << (uint64_t)prop.TextureHandle;
            }

            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;

        std::ofstream fout(filepath);
        fout << out.c_str();
    }

    bool MaterialSerializer::Deserialize(const std::shared_ptr<Material>& material, const std::string& filepath) {
        YAML::Node data;
        try {
            data = YAML::LoadFile(filepath);
        } catch (std::exception& e) {
            return false;
        }

        if (!data["MaterialName"]) return false;

        material->Name = data["MaterialName"].as<std::string>();
        material->ShaderName = data["ShaderName"].as<std::string>();
        material->AssetPath = filepath;

        // BlendMode — optional, defaults to Opaque for backward compat
        if (data["BlendMode"]) {
            int bm = data["BlendMode"].as<int>();
            if (bm == 2) bm = 3; // migrate old buggy Translucent value
            material->SetBlendMode(static_cast<MaterialBlendMode>(bm));
        } else
            material->SetBlendMode(MaterialBlendMode::Opaque);

        // AlphaCutoff — optional, defaults to 0.5
        if (data["AlphaCutoff"])
            material->SetAlphaCutoff(data["AlphaCutoff"].as<float>());

        // LightModes — optional SRP tag (comma-separated or YAML scalar), auto-detect from BlendMode if missing
        if (data["LightModes"] && data["LightModes"].IsScalar())
            material->SetLightModeStr(data["LightModes"].as<std::string>());
        else if (data["LightMode"] && data["LightMode"].IsScalar())
            material->SetLightModeStr(data["LightMode"].as<std::string>()); // legacy compat

        material->Properties.clear(); 

        auto propertiesNode = data["Properties"];
        if (propertiesNode) {
            for (auto propNode : propertiesNode) {
                MaterialProperty prop;
                prop.UniformName = propNode["UniformName"].as<std::string>();
                prop.DisplayName = propNode["DisplayName"].as<std::string>();
                prop.Type = (MaterialPropertyType)propNode["Type"].as<int>();

                if (prop.Type == MaterialPropertyType::Float && propNode["FloatValue"]) prop.FloatValue = propNode["FloatValue"].as<float>();
                if (prop.Type == MaterialPropertyType::Int && propNode["IntValue"]) prop.IntValue = propNode["IntValue"].as<int>();
                if (prop.Type == MaterialPropertyType::Bool && propNode["BoolValue"]) prop.BoolValue = propNode["BoolValue"].as<bool>();

                if (prop.Type == MaterialPropertyType::Vec2 && propNode["Vec2Value"]) {
                    auto vecNode = propNode["Vec2Value"];
                    prop.Vec2Value = glm::vec2(vecNode[0].as<float>(), vecNode[1].as<float>());
                }
                if (prop.Type == MaterialPropertyType::Vec3 && propNode["Vec3Value"]) {
                    auto vecNode = propNode["Vec3Value"];
                    prop.Vec3Value = glm::vec3(vecNode[0].as<float>(), vecNode[1].as<float>(), vecNode[2].as<float>());
                }
                if (prop.Type == MaterialPropertyType::Vec4 && propNode["Vec4Value"]) {
                    auto vecNode = propNode["Vec4Value"];
                    prop.Vec4Value = glm::vec4(vecNode[0].as<float>(), vecNode[1].as<float>(), vecNode[2].as<float>(), vecNode[3].as<float>());
                }

                // ==========================================
                // 【核心修复 2】：读取时直接获取 TextureHandle
                // ==========================================
                if (prop.Type == MaterialPropertyType::Texture2D || prop.Type == MaterialPropertyType::TextureCube) {
                    // 优先读取新的 UUID 架构
                    if (propNode["TextureHandle"]) {
                        prop.TextureHandle = propNode["TextureHandle"].as<uint64_t>();
                    } 
                    // 兼容极古老版本的数据：如果发现了旧的 TexturePath，直接归零，防止崩溃
                    else if (propNode["TexturePath"]) {
                        prop.TextureHandle = 0;
                    }
                }

                material->Properties.push_back(prop);
            }
        }

        if (material->FromAyaShader) return true; // skip default PBR injection

        // Migrate: ensure alpha properties exist (old .mat files lack them)
        auto hasProp = [&](const std::string& name) {
            for (auto& p : material->Properties)
                if (p.UniformName == name) return true;
            return false;
        };
        auto addFloat = [&](const char* name, const char* display, float val) {
            if (!hasProp(name)) {
                MaterialProperty p;
                p.UniformName = name; p.DisplayName = display;
                p.Type = MaterialPropertyType::Float; p.FloatValue = val;
                material->Properties.push_back(p);
            }
        };
        auto addBool = [&](const char* name, const char* display, bool val) {
            if (!hasProp(name)) {
                MaterialProperty p;
                p.UniformName = name; p.DisplayName = display;
                p.Type = MaterialPropertyType::Bool; p.BoolValue = val;
                material->Properties.push_back(p);
            }
        };
        auto addTex = [&](const char* name, const char* display) {
            if (!hasProp(name)) {
                MaterialProperty p;
                p.UniformName = name; p.DisplayName = display;
                p.Type = MaterialPropertyType::Texture2D; p.TextureHandle = 0;
                material->Properties.push_back(p);
            }
        };
        addFloat("u_Alpha",   "Alpha Multiplier",  1.0f);
        addTex  ("u_AlphaMap","Alpha/Opacity Map");
        // ORM packed texture (UE4: R=AO, G=Roughness, B=Metallic)
        addTex  ("u_ORMMap",  "ORM Texture (R=AO G=Roughness B=Metallic)");
        // Height / displacement (reserved for future implementation)
        addTex  ("u_HeightMap",  "Height/Displacement Map");
        // Emissive (reserved for future implementation)
        addTex  ("u_EmissiveMap","Emissive Map");

        // Restore Packing after all properties are loaded (defaults to UE4_ORM)
        if (data["Packing"]) {
            uint32_t p = data["Packing"].as<uint32_t>();
            if (p <= 2) material->SetPacking(static_cast<Material::TexturePacking>(p));
        }

        return true;
    }
}