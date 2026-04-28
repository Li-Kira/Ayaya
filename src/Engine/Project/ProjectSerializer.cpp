#include "ayapch.h"
#include "ProjectSerializer.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>

namespace Ayaya {

    ProjectSerializer::ProjectSerializer(std::shared_ptr<Project> project)
        : m_Project(project) {}

    bool ProjectSerializer::Serialize(const std::filesystem::path& filepath) {
        const auto& config = m_Project->GetConfig();
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Project" << YAML::Value;
        out << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << config.Name;
        out << YAML::Key << "StartScene" << YAML::Value << config.StartScene;
        out << YAML::Key << "AssetDirectory" << YAML::Value << config.AssetDirectory;
        out << YAML::EndMap;
        out << YAML::EndMap;

        std::ofstream fout(filepath);
        fout << out.c_str();
        return true;
    }

    bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath) {
        std::ifstream stream(filepath);
        std::stringstream strStream;
        strStream << stream.rdbuf();

        YAML::Node data;
        try { data = YAML::Load(strStream.str()); }
        catch (YAML::ParserException e) { return false; }

        auto projectNode = data["Project"];
        if (!projectNode) return false;

        auto& config = m_Project->GetConfig();
        config.Name = projectNode["Name"].as<std::string>();
        config.StartScene = projectNode["StartScene"].as<std::string>();
        config.AssetDirectory = projectNode["AssetDirectory"].as<std::string>();

        return true;
    }
}