#pragma once

#include "Easing.hpp"

namespace Ayaya {

    // ==========================================
    // EasingType — serializable enum + lookup table
    // Separated from Easing.hpp to avoid ODR issues with static array
    // ==========================================
    enum class EasingType {
        Linear,
        EaseInQuad, EaseOutQuad, EaseInOutQuad,
        EaseInCubic, EaseOutCubic, EaseInOutCubic,
        EaseInQuart, EaseOutQuart, EaseInOutQuart,
        EaseInQuint, EaseOutQuint, EaseInOutQuint,
        EaseInSine, EaseOutSine, EaseInOutSine,
        EaseInExpo, EaseOutExpo, EaseInOutExpo,
        EaseInCirc, EaseOutCirc, EaseInOutCirc,
        EaseInBack, EaseOutBack, EaseInOutBack,
        EaseInElastic, EaseOutElastic, EaseInOutElastic,
        EaseInBounce, EaseOutBounce, EaseInOutBounce,
    };

    inline float(*GetEasingFunc(EasingType type))(float) {
        static float(*const table[])(float) = {
            Easing::Linear,
            Easing::EaseInQuad, Easing::EaseOutQuad, Easing::EaseInOutQuad,
            Easing::EaseInCubic, Easing::EaseOutCubic, Easing::EaseInOutCubic,
            Easing::EaseInQuart, Easing::EaseOutQuart, Easing::EaseInOutQuart,
            Easing::EaseInQuint, Easing::EaseOutQuint, Easing::EaseInOutQuint,
            Easing::EaseInSine, Easing::EaseOutSine, Easing::EaseInOutSine,
            Easing::EaseInExpo, Easing::EaseOutExpo, Easing::EaseInOutExpo,
            Easing::EaseInCirc, Easing::EaseOutCirc, Easing::EaseInOutCirc,
            Easing::EaseInBack, Easing::EaseOutBack, Easing::EaseInOutBack,
            Easing::EaseInElastic, Easing::EaseOutElastic, Easing::EaseInOutElastic,
            Easing::EaseInBounce, Easing::EaseOutBounce, Easing::EaseInOutBounce,
        };
        return table[static_cast<int>(type)];
    }

    static constexpr int kEasingTypeCount = 31;

} // namespace Ayaya
