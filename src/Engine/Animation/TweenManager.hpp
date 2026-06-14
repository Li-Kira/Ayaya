#pragma once

#include "Tween.hpp"
#include "Core/Timestep.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace Ayaya {

    // ==========================================
    // TweenManager — global tween executor
    // Owns all active tweens; called each frame from Update().
    // Handles deferred create/cancel during iteration for iterator safety.
    // ==========================================
    class TweenManager {
    public:
        TweenManager() = default;
        ~TweenManager() = default;

        // ---- Tween creation ----
        template<typename T>
        uint32_t CreateTween(const T& from, const T& to, float duration,
                             std::function<void(const T&)> setter,
                             EasingType easing = EasingType::Linear,
                             std::function<void()> onComplete = nullptr);

        // ---- Lifecycle ----
        void Update(Timestep ts);

        void Cancel(uint32_t handle);
        void Pause(uint32_t handle);
        void Resume(uint32_t handle);
        void CancelAll();

        size_t ActiveCount() const;  // Playing + Paused (not Finished/removed)

    private:
        // ---- Type-erased tween interface ----
        struct ITween {
            virtual ~ITween() = default;
            virtual bool Update(float dt) = 0;     // returns true if still alive
            virtual void SetPaused(bool paused) = 0;
            virtual bool IsPaused() const = 0;
            uint32_t Handle = 0;
            bool MarkedForRemoval = false;
        };

        template<typename T>
        struct TweenImpl : ITween {
            TweenTask<T> Task;

            bool Update(float dt) override {
                if (Task.State != TweenState::Playing) return true;
                Task.Elapsed += dt;
                float t = Task.Elapsed / Task.Duration;
                if (t >= 1.0f) {
                    t = 1.0f;
                    Task.State = TweenState::Finished;
                }
                float easedT = Task.EasingFunc ? Task.EasingFunc(t) : t;
                T current = Easing::Lerp(Task.StartValue, Task.EndValue, easedT);
                Task.Setter(current);
                if (Task.State == TweenState::Finished) {
                    if (Task.OnComplete) Task.OnComplete();
                    return false;
                }
                return true;
            }

            void SetPaused(bool paused) override {
                Task.State = paused ? TweenState::Paused : TweenState::Playing;
            }

            bool IsPaused() const override {
                return Task.State == TweenState::Paused;
            }
        };

        void FlushPending();

        std::unordered_map<uint32_t, std::unique_ptr<ITween>> m_Tweens;
        std::vector<std::unique_ptr<ITween>> m_PendingCreates;
        uint32_t m_NextHandle = 1;
    };

    // ==========================================
    // Template implementations (must be in header)
    // ==========================================

    template<typename T>
    uint32_t TweenManager::CreateTween(const T& from, const T& to, float duration,
                                        std::function<void(const T&)> setter,
                                        EasingType easing,
                                        std::function<void()> onComplete) {
        auto impl = std::make_unique<TweenImpl<T>>();
        impl->Handle = m_NextHandle++;
        impl->Task.StartValue  = from;
        impl->Task.EndValue    = to;
        impl->Task.Duration    = (duration > 0.0f) ? duration : 0.001f; // avoid div-by-zero
        impl->Task.EasingFunc  = GetEasingFunc(easing);
        impl->Task.Setter      = std::move(setter);
        impl->Task.OnComplete  = std::move(onComplete);
        impl->Task.State       = TweenState::Playing;

        uint32_t handle = impl->Handle;
        m_Tweens[handle] = std::move(impl);
        return handle;
    }

    // ---- DoTween convenience ----
    template<typename T>
    uint32_t DoTween(TweenManager& tm,
                     const T& from, const T& to, float duration,
                     std::function<void(const T&)> setter,
                     EasingType easing,
                     std::function<void()> onComplete) {
        return tm.CreateTween(from, to, duration, std::move(setter), easing, std::move(onComplete));
    }

} // namespace Ayaya
