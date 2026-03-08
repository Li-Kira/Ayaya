#include "ayapch.h"
#include "MaterialSerializer.hpp"
#include "Asset/AssetManager.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>

namespace Ayaya {

    void MaterialSerializer::Serialize(const std::shared_ptr<Material>& material, const std::string& filepath) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "MaterialName" << YAML::Value << material->Name;
        out << YAML::Key << "ShaderName" << YAML::Value << material->ShaderName;
        
        out << YAML::Key << "Properties" << YAML::BeginSeq;
        for (auto& prop : material->Properties) {
            out << YAML::BeginMap;
            out << YAML::Key << "UniformName" << YAML::Value << prop.UniformName;
            out << YAML::Key << "DisplayName" << YAML::Value << prop.DisplayName;
            out << YAML::Key << "Type" << YAML::Value << (int)prop.Type;

            // ==========================================
            // 核心修复：支持所有数据类型的保存！
            // ==========================================
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
            
            if (prop.Type == MaterialPropertyType::Texture2D) 
                out << YAML::Key << "TexturePath" << YAML::Value << prop.TexturePath;

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
        } catch (YAML::ParserException& e) {
            AYAYA_CORE_ERROR("Failed to load .mat file: {0}", filepath);
            return false;
        }

        if (!data["MaterialName"]) return false;

        material->Name = data["MaterialName"].as<std::string>();
        material->ShaderName = data["ShaderName"].as<std::string>();
        material->AssetPath = filepath; // 记录路径
        material->Properties.clear();   // 清空旧属性，完全由文件接管！

        auto propertiesNode = data["Properties"];
        if (propertiesNode) {
            for (auto propNode : propertiesNode) {
                MaterialProperty prop;
                prop.UniformName = propNode["UniformName"].as<std::string>();
                prop.DisplayName = propNode["DisplayName"].as<std::string>();
                prop.Type = (MaterialPropertyType)propNode["Type"].as<int>();

                // ==========================================
                // 核心修复：支持所有数据类型的读取！
                // ==========================================
                if (prop.Type == MaterialPropertyType::Float && propNode["FloatValue"]) 
                    prop.FloatValue = propNode["FloatValue"].as<float>();
                
                if (prop.Type == MaterialPropertyType::Int && propNode["IntValue"]) 
                    prop.IntValue = propNode["IntValue"].as<int>();
                    
                if (prop.Type == MaterialPropertyType::Bool && propNode["BoolValue"]) 
                    prop.BoolValue = propNode["BoolValue"].as<bool>();

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

                if (prop.Type == MaterialPropertyType::Texture2D) {
                    if (propNode["TexturePath"]) {
                        prop.TexturePath = propNode["TexturePath"].as<std::string>();
                        if (!prop.TexturePath.empty()) {
                            prop.TextureHandle = AssetManager::ImportAsset(prop.TexturePath);
                        }
                    }
                }

                material->Properties.push_back(prop);
            }
        }
        return true;
    }
}