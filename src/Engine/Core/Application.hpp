#pragma once

#include "Window.hpp"
#include <memory>

namespace Ayaya {

    class Application {
    public:
        Application();
        virtual ~Application();

        void Run();

    private:
        std::unique_ptr<Window> m_Window;
        bool m_Running = true;
    };

}