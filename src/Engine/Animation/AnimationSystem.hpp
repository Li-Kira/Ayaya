#pragma once

namespace Ayaya {

    class Scene;

    // ==========================================
    // AnimationSystem — evaluates AnimationControllerComponent tracks
    // Called from Scene::OnUpdateRuntime() and EditorLayer preview.
    // ==========================================
    class AnimationSystem {
    public:
        static void Update(Scene& scene, float currentTime);
    };

} // namespace Ayaya
