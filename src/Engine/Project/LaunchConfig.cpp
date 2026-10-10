#include "ayapch.h"
#include "LaunchConfig.hpp"
#include "Core/Log.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>

namespace Ayaya {

    bool LaunchConfig::Load(const std::filesystem::path& filepath, LaunchConfig& out) {
        if (!std::filesystem::exists(filepath)) {
            AYAYA_CORE_ERROR("[LaunchConfig] Missing game config: {0}", filepath.string());
            return false;
        }

        try {
            YAML::Node root = YAML::LoadFile(filepath.string());

            if (auto game = root["Game"]) {
                out.GameName         = game["Name"]            ? game["Name"].as<std::string>()            : out.GameName;
                out.ProjectFile      = game["ProjectFile"]     ? game["ProjectFile"].as<std::string>()     : out.ProjectFile;
                out.StartScene       = game["StartScene"]      ? game["StartScene"].as<std::string>()      : out.StartScene;
                out.AssetDirectory   = game["AssetDirectory"]  ? game["AssetDirectory"].as<std::string>()  : out.AssetDirectory;
                out.DefaultSRPScript = game["DefaultSRPScript"] ? game["DefaultSRPScript"].as<uint64_t>()   : out.DefaultSRPScript;
            }
            if (auto win = root["Window"]) {
                out.WindowWidth  = win["Width"]  ? win["Width"].as<uint32_t>()  : out.WindowWidth;
                out.WindowHeight = win["Height"] ? win["Height"].as<uint32_t>() : out.WindowHeight;
                out.WindowTitle  = win["Title"]  ? win["Title"].as<std::string>() : out.WindowTitle;
                out.VSync        = win["VSync"]  ? win["VSync"].as<bool>()       : out.VSync;
            }
            if (auto rnd = root["Renderer"]) {
                out.GraphicsAPI = rnd["API"] ? rnd["API"].as<std::string>() : out.GraphicsAPI;
            }
        } catch (const YAML::Exception& e) {
            AYAYA_CORE_ERROR("[LaunchConfig] Failed to parse {0}: {1}", filepath.string(), e.what());
            return false;
        }
        return true;
    }

    bool LaunchConfig::Save(const std::filesystem::path& filepath, const LaunchConfig& config) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Game" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << config.GameName;
        out << YAML::Key << "ProjectFile" << YAML::Value << config.ProjectFile;
        out << YAML::Key << "StartScene" << YAML::Value << config.StartScene;
        out << YAML::Key << "AssetDirectory" << YAML::Value << config.AssetDirectory;
        out << YAML::Key << "DefaultSRPScript" << YAML::Value << config.DefaultSRPScript;
        out << YAML::EndMap;

        out << YAML::Key << "Window" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Width" << YAML::Value << config.WindowWidth;
        out << YAML::Key << "Height" << YAML::Value << config.WindowHeight;
        out << YAML::Key << "Title" << YAML::Value << config.WindowTitle;
        out << YAML::Key << "VSync" << YAML::Value << config.VSync;
        out << YAML::EndMap;

        out << YAML::Key << "Renderer" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "API" << YAML::Value << config.GraphicsAPI;
        out << YAML::EndMap;
        out << YAML::EndMap;

        std::ofstream fout(filepath);
        if (!fout.is_open()) {
            AYAYA_CORE_ERROR("[LaunchConfig] Failed to open {0} for writing", filepath.string());
            return false;
        }
        fout << out.c_str();
        return true;
    }

} // namespace Ayaya
