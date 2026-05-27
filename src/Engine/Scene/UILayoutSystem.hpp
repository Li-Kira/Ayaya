#pragma once

#include "Scene.hpp"

namespace Ayaya {

    class UILayoutSystem {
    public:
        // 遍历场景中所有 CanvasComponent 子树，递归计算全部 UI 布局。
        static void Update(Scene& scene, uint32_t viewportWidth, uint32_t viewportHeight);
    };

}
