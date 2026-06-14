#pragma once

#include "EasingType.hpp"
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Ayaya {

    enum class TweenState {
        Playing,
        Paused,
        Finished
    };

    // ==========================================
    // TweenTask<T> — describes a single tween from A to B
    // T is typically float, glm::vec3, glm::quat, etc.
    // ==========================================
    template<typename T>
    struct TweenTask {
        T       StartValue;
        T       EndValue;
        float   Duration  = 1.0f;
        float   Elapsed   = 0.0f;
        float (*EasingFunc)(float) = nullptr;

        std::function<void(const T&)> Setter;      // called each frame with interpolated value
        std::function<void()>         OnComplete;  // called once when tween finishes

        TweenState State = TweenState::Playing;
    };

    // Forward declare TweenManager (see TweenManager.hpp)
    class TweenManager;

    // ==========================================
    // Convenience: DoTween creates and registers a tween in one call
    // ==========================================
    template<typename T>
    uint32_t DoTween(TweenManager& tm,
                     const T& from, const T& to, float duration,
                     std::function<void(const T&)> setter,
                     EasingType easing = EasingType::Linear,
                     std::function<void()> onComplete = nullptr);

    // Common type aliases for brevity
    using FloatTween  = TweenTask<float>;
    using Vec2Tween   = TweenTask<glm::vec2>;
    using Vec3Tween   = TweenTask<glm::vec3>;
    using Vec4Tween   = TweenTask<glm::vec4>;
    using QuatTween   = TweenTask<glm::quat>;

} // namespace Ayaya
