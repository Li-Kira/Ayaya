#include "LayerStack.hpp"

namespace Ayaya {

    LayerStack::LayerStack() {}

    LayerStack::~LayerStack() {
        for (Layer* layer : m_Layers)
            delete layer;
    }

    void LayerStack::PushLayer(Layer* layer) {
        m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
        m_LayerInsertIndex++;
        layer->OnAttach();
    }

    void LayerStack::PushOverlay(Layer* overlay) {
        m_Layers.emplace_back(overlay);
        overlay->OnAttach();
    }

    // Pop 逻辑略，主要是在 vector 中查找并调用 OnDetach
}