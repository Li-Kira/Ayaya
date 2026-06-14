#include "CurveSerializer.hpp"
#include "Core/Log.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>

namespace Ayaya {

    bool CurveSerializer::Serialize(const CurveAsset& curve, const std::filesystem::path& filepath) {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Curve" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Keys" << YAML::Value << YAML::BeginSeq;

        for (const auto& key : curve.Keys) {
            out << YAML::BeginMap;
            out << YAML::Key << "Time"       << YAML::Value << key.Time;
            out << YAML::Key << "Value"      << YAML::Value << key.Value;
            out << YAML::Key << "InTangent"  << YAML::Value << key.InTangent;
            out << YAML::Key << "OutTangent" << YAML::Value << key.OutTangent;
            out << YAML::EndMap;
        }

        out << YAML::EndSeq;
        out << YAML::EndMap; // Curve
        out << YAML::EndMap; // root

        std::ofstream fout(filepath);
        if (!fout.is_open()) {
            AYAYA_CORE_ERROR("CurveSerializer: Failed to open file for writing: {0}", filepath.string());
            return false;
        }
        fout << out.c_str();
        return true;
    }

    bool CurveSerializer::Deserialize(std::shared_ptr<CurveAsset> curve, const std::filesystem::path& filepath) {
        if (!curve) return false;

        try {
            YAML::Node data = YAML::LoadFile(filepath.string());
            auto curveNode = data["Curve"];
            if (!curveNode) {
                AYAYA_CORE_WARN("CurveSerializer: Missing 'Curve' key in {0}", filepath.string());
                return false;
            }

            auto keysNode = curveNode["Keys"];
            if (!keysNode || !keysNode.IsSequence()) {
                AYAYA_CORE_WARN("CurveSerializer: Missing or invalid 'Keys' sequence in {0}", filepath.string());
                return false;
            }

            curve->Keys.clear();
            for (const auto& keyNode : keysNode) {
                Keyframe kf;
                kf.Time       = keyNode["Time"]       ? keyNode["Time"].as<float>()       : 0.0f;
                kf.Value      = keyNode["Value"]      ? keyNode["Value"].as<float>()      : 0.0f;
                kf.InTangent  = keyNode["InTangent"]  ? keyNode["InTangent"].as<float>()  : 0.0f;
                kf.OutTangent = keyNode["OutTangent"] ? keyNode["OutTangent"].as<float>() : 0.0f;
                curve->Keys.push_back(kf);
            }

            // Ensure sorted order after deserialization
            std::sort(curve->Keys.begin(), curve->Keys.end(),
                [](const Keyframe& a, const Keyframe& b) { return a.Time < b.Time; });

            return true;
        } catch (const YAML::Exception& e) {
            AYAYA_CORE_ERROR("CurveSerializer: YAML parse error for {0}: {1}", filepath.string(), e.what());
            return false;
        }
    }

} // namespace Ayaya
