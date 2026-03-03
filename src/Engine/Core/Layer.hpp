#pragma once

#include <string>
#include "Timestep.hpp" // 用于处理帧间隔时间

namespace Ayaya {

    class Layer {
    public:
        Layer(const std::string& name = "Layer");
        virtual ~Layer();

        virtual void OnAttach() {}    // 当层被推入栈时调用
        virtual void OnDetach() {}    // 当层被移除时调用
        virtual void OnUpdate(Timestep ts) {}    // 每帧更新
        virtual void OnImGuiRender() {} // 专门用于渲染 UI

        inline const std::string& GetName() const { return m_DebugName; }
    protected:
        std::string m_DebugName;
    };

}