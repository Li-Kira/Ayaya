#pragma once

#include <vector>
#include <algorithm>
#include <cfloat>
#include "Core/UUID.hpp"

namespace Ayaya {

    // ==========================================
    // Keyframe — single control point on a curve
    // ==========================================
    struct Keyframe {
        float Time        = 0.0f;
        float Value       = 0.0f;
        float InTangent   = 0.0f;   // left handle slope
        float OutTangent  = 0.0f;   // right handle slope
    };

    // ==========================================
    // CurveAsset — Hermite-interpolated animation curve
    // ==========================================
    class CurveAsset {
    public:
        std::vector<Keyframe> Keys;

        // Evaluate the curve at time t using Hermite interpolation.
        // Before first key: returns first key's value.
        // After last key: returns last key's value.
        float Evaluate(float t) const;

        // Insert a keyframe at the correct sorted position (O(log n) via upper_bound).
        // Returns the insertion index.
        int  AddKey(float time, float value, float inTangent = 0.0f, float outTangent = 0.0f);

        // Remove a keyframe by index.
        void RemoveKey(int index);

        bool IsValid() const { return !Keys.empty(); }

        UUID Handle = 0;
    };

    // ---- Inline implementations ----

    inline float CurveAsset::Evaluate(float t) const {
        if (Keys.empty()) return 0.0f;
        if (Keys.size() == 1) return Keys[0].Value;

        // Before first key
        if (t <= Keys[0].Time) return Keys[0].Value;

        // After last key
        if (t >= Keys.back().Time) return Keys.back().Value;

        // Find the two surrounding keys via binary search
        int lo = 0, hi = (int)Keys.size() - 1;
        while (lo < hi - 1) {
            int mid = (lo + hi) / 2;
            if (Keys[mid].Time <= t) lo = mid;
            else                     hi = mid;
        }
        const auto& k0 = Keys[lo];
        const auto& k1 = Keys[hi];

        // Hermite interpolation
        float dx = k1.Time - k0.Time;
        if (dx <= 0.0f) return k0.Value; // degenerate: identical times

        float d  = (t - k0.Time) / dx;
        float d2 = d * d;
        float d3 = d2 * d;

        // Hermite basis functions
        float h00 =  2.0f * d3 - 3.0f * d2 + 1.0f;   // (1 - 3t² + 2t³)
        float h10 =        d3 - 2.0f * d2 + d;         // (t - 2t² + t³)
        float h01 = -2.0f * d3 + 3.0f * d2;            // (3t² - 2t³)
        float h11 =        d3 -       d2;               // (t³ - t²)

        return h00 * k0.Value
             + h10 * k0.OutTangent * dx
             + h01 * k1.Value
             + h11 * k1.InTangent * dx;
    }

    inline int CurveAsset::AddKey(float time, float value, float inTangent, float outTangent) {
        Keyframe kf{ time, value, inTangent, outTangent };

        // Find insertion position via upper_bound on Time
        auto it = std::upper_bound(Keys.begin(), Keys.end(), kf,
            [](const Keyframe& a, const Keyframe& b) { return a.Time < b.Time; });

        int index = (int)(it - Keys.begin());
        Keys.insert(it, kf);
        return index;
    }

    inline void CurveAsset::RemoveKey(int index) {
        if (index >= 0 && index < (int)Keys.size())
            Keys.erase(Keys.begin() + index);
    }

} // namespace Ayaya
