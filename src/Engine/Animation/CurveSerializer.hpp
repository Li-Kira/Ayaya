#pragma once

#include "CurveAsset.hpp"
#include <filesystem>
#include <memory>

namespace Ayaya {

    // ==========================================
    // CurveSerializer — static YAML serialization
    // Follows MaterialSerializer pattern.
    // ==========================================
    class CurveSerializer {
    public:
        static bool Serialize(const CurveAsset& curve, const std::filesystem::path& filepath);
        static bool Deserialize(std::shared_ptr<CurveAsset> curve, const std::filesystem::path& filepath);
    };

} // namespace Ayaya
