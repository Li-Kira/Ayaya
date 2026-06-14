#include "TweenManager.hpp"

namespace Ayaya {

    void TweenManager::Update(Timestep ts) {
        float dt = ts.GetSeconds();

        for (auto& [handle, tween] : m_Tweens) {
            if (tween->MarkedForRemoval) continue;
            if (tween->IsPaused()) continue;
            bool alive = tween->Update(dt);
            if (!alive)
                tween->MarkedForRemoval = true;
        }

        FlushPending();
    }

    void TweenManager::Cancel(uint32_t handle) {
        auto it = m_Tweens.find(handle);
        if (it != m_Tweens.end())
            it->second->MarkedForRemoval = true;
    }

    void TweenManager::Pause(uint32_t handle) {
        auto it = m_Tweens.find(handle);
        if (it != m_Tweens.end())
            it->second->SetPaused(true);
    }

    void TweenManager::Resume(uint32_t handle) {
        auto it = m_Tweens.find(handle);
        if (it != m_Tweens.end())
            it->second->SetPaused(false);
    }

    void TweenManager::CancelAll() {
        for (auto& [handle, tween] : m_Tweens)
            tween->MarkedForRemoval = true;
        FlushPending();
    }

    size_t TweenManager::ActiveCount() const {
        size_t count = 0;
        for (const auto& [handle, tween] : m_Tweens)
            if (!tween->MarkedForRemoval) count++;
        return count;
    }

    void TweenManager::FlushPending() {
        for (auto it = m_Tweens.begin(); it != m_Tweens.end(); ) {
            if (it->second->MarkedForRemoval)
                it = m_Tweens.erase(it);
            else
                ++it;
        }
    }

} // namespace Ayaya
