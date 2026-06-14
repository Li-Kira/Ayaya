#pragma once

// ==========================================
// Easing.hpp — Robert Penner easing functions
// All functions take t in [0, 1] and return the eased value.
// Output may overshoot 1.0 for Elastic/Back types.
// ==========================================

#include <cmath>
#include <glm/gtc/quaternion.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace Ayaya::Easing {

    // ---- Linear ----
    inline float Linear(float t) { return t; }

    // ---- Quad ----
    inline float EaseInQuad(float t)  { return t * t; }
    inline float EaseOutQuad(float t) { return t * (2.0f - t); }
    inline float EaseInOutQuad(float t) {
        return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
    }

    // ---- Cubic ----
    inline float EaseInCubic(float t)  { return t * t * t; }
    inline float EaseOutCubic(float t) { float t1 = t - 1.0f; return t1 * t1 * t1 + 1.0f; }
    inline float EaseInOutCubic(float t) {
        return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
    }

    // ---- Quart ----
    inline float EaseInQuart(float t)  { return t * t * t * t; }
    inline float EaseOutQuart(float t) { float t1 = t - 1.0f; return 1.0f - t1 * t1 * t1 * t1; }
    inline float EaseInOutQuart(float t) {
        return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - 8.0f * (t - 1.0f) * (t - 1.0f) * (t - 1.0f) * (t - 1.0f);
    }

    // ---- Quint ----
    inline float EaseInQuint(float t)  { return t * t * t * t * t; }
    inline float EaseOutQuint(float t) { float t1 = t - 1.0f; return 1.0f + t1 * t1 * t1 * t1 * t1; }
    inline float EaseInOutQuint(float t) {
        return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f + 16.0f * (t - 1.0f) * (t - 1.0f) * (t - 1.0f) * (t - 1.0f) * (t - 1.0f);
    }

    // ---- Sine ----
    inline float EaseInSine(float t)  { return 1.0f - std::cos(t * M_PI * 0.5f); }
    inline float EaseOutSine(float t) { return std::sin(t * M_PI * 0.5f); }
    inline float EaseInOutSine(float t) { return -(std::cos(M_PI * t) - 1.0f) * 0.5f; }

    // ---- Expo ----
    inline float EaseInExpo(float t)  { return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f)); }
    inline float EaseOutExpo(float t) { return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
    inline float EaseInOutExpo(float t) {
        if (t == 0.0f || t == 1.0f) return t;
        return t < 0.5f ? 0.5f * std::pow(2.0f, 20.0f * t - 10.0f)
                        : -0.5f * std::pow(2.0f, -20.0f * t + 10.0f) + 1.0f;
    }

    // ---- Circ ----
    inline float EaseInCirc(float t)  { return 1.0f - std::sqrt(1.0f - t * t); }
    inline float EaseOutCirc(float t) { return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f)); }
    inline float EaseInOutCirc(float t) {
        return t < 0.5f ? (1.0f - std::sqrt(1.0f - 4.0f * t * t)) * 0.5f
                        : (std::sqrt(1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f)) + 1.0f) * 0.5f;
    }

    // ---- Back ----
    inline float EaseInBack(float t)  { const float s = 1.70158f; return t * t * ((s + 1.0f) * t - s); }
    inline float EaseOutBack(float t) { const float s = 1.70158f; float t1 = t - 1.0f; return t1 * t1 * ((s + 1.0f) * t1 + s) + 1.0f; }
    inline float EaseInOutBack(float t) {
        const float s = 1.70158f * 1.525f;
        return t < 0.5f ? (t * 2.0f * t * 2.0f * ((s + 1.0f) * t * 2.0f - s)) * 0.5f
                        : ((t * 2.0f - 2.0f) * (t * 2.0f - 2.0f) * ((s + 1.0f) * (t * 2.0f - 2.0f) + s) + 2.0f) * 0.5f;
    }

    // ---- Elastic ----
    inline float EaseInElastic(float t) {
        if (t == 0.0f || t == 1.0f) return t;
        return -std::pow(2.0f, 10.0f * (t - 1.0f)) * std::sin((t - 1.0f - 0.075f) * (2.0f * M_PI) / 0.3f);
    }
    inline float EaseOutElastic(float t) {
        if (t == 0.0f || t == 1.0f) return t;
        return std::pow(2.0f, -10.0f * t) * std::sin((t - 0.075f) * (2.0f * M_PI) / 0.3f) + 1.0f;
    }
    inline float EaseInOutElastic(float t) {
        if (t == 0.0f || t == 1.0f) return t;
        return t < 0.5f ? -0.5f * std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * (2.0f * M_PI) / 4.5f)
                        :  0.5f * std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * (2.0f * M_PI) / 4.5f) + 1.0f;
    }

    // ---- Bounce ----
    inline float EaseOutBounce(float t) {
        if      (t < 1.0f / 2.75f) return 7.5625f * t * t;
        else if (t < 2.0f / 2.75f) { t -= 1.5f / 2.75f; return 7.5625f * t * t + 0.75f; }
        else if (t < 2.5f / 2.75f) { t -= 2.25f / 2.75f; return 7.5625f * t * t + 0.9375f; }
        else                         { t -= 2.625f / 2.75f; return 7.5625f * t * t + 0.984375f; }
    }
    inline float EaseInBounce(float t)  { return 1.0f - EaseOutBounce(1.0f - t); }
    inline float EaseInOutBounce(float t) {
        return t < 0.5f ? EaseInBounce(t * 2.0f) * 0.5f
                        : EaseOutBounce(t * 2.0f - 1.0f) * 0.5f + 0.5f;
    }

    // ==========================================
    // Generic Lerp — function overloading for glm::quat
    // ==========================================

    // Base template for float, vec2/3/4 (glm overloads arithmetic ops)
    template<typename T>
    inline T Lerp(const T& a, const T& b, float t) {
        return a + (b - a) * t;
    }

    // Overload for quaternion spherical interpolation (NOT template specialization)
    inline glm::quat Lerp(const glm::quat& a, const glm::quat& b, float t) {
        return glm::slerp(a, b, t);
    }

} // namespace Ayaya::Easing
