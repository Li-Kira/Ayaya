#include "ayapch.h"
#include "ProjectSerializer.hpp"
#include "Project.hpp"         // 【核心修复 1】：必须包含完整定义，否则编译器不知道有 GetConfig
#include <yaml-cpp/yaml.h>
#include <fstream>

namespace Ayaya {

    ProjectSerializer::ProjectSerializer(std::shared_ptr<Project> project)
        : m_Project(project) {}

    bool ProjectSerializer::Serialize(const std::filesystem::path& filepath) {
        // 【核心修复 2】：使用 -> 而不是 . 来访问 shared_ptr 指向的对象
        if (!m_Project) return false;

        const auto& config = m_Project->GetConfig(); // 这里现在可以正确初始化引用了
        
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Project" << YAML::Value;
        out << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << config.Name;
        out << YAML::Key << "StartScene" << YAML::Value << config.StartScene;
        out << YAML::Key << "AssetDirectory" << YAML::Value << config.AssetDirectory;
        out << YAML::Key << "DefaultSRPScript" << YAML::Value << config.DefaultSRPScript;
        out << YAML::EndMap;
        out << YAML::EndMap;

        std::ofstream fout(filepath);
        if (!fout.is_open()) return false;
        
        fout << out.c_str();
        return true;
    }

    bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath) {
        if (!m_Project) return false;

        std::ifstream stream(filepath);
        if (!stream.is_open()) return false;

        std::stringstream strStream;
        strStream << stream.rdbuf();

        YAML::Node data;
        try { 
            data = YAML::Load(strStream.str()); 
        } catch (const YAML::ParserException& e) { 
            AYAYA_CORE_ERROR("Failed to load project file: {0}", e.what());
            return false; 
        }

        auto projectNode = data["Project"];
        if (!projectNode) return false;

        // 【核心修复 3】：同样使用 -> 访问配置
        auto& config = m_Project->GetConfig();
        config.Name = projectNode["Name"].as<std::string>();
        config.StartScene = projectNode["StartScene"].as<std::string>();
        config.AssetDirectory = projectNode["AssetDirectory"].as<std::string>();
        if (projectNode["DefaultSRPScript"])
            config.DefaultSRPScript = projectNode["DefaultSRPScript"].as<uint64_t>();

        return true;
    }
}