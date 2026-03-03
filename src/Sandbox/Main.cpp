#include <Core/Application.hpp>
#include "ExampleLayer.hpp"

int main() {
    Ayaya::Application app;
    app.PushLayer(new ExampleLayer());
    app.Run();
    return 0;
}