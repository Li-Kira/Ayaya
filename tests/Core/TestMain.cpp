/**
 * Headless test environment setup.
 *
 * 1. Override Input::s_Instance to nullptr — the Ayaya.lib definition
 *    (`new OpenGLInput()`) requires a GLFW window which doesn't exist in
 *    tests. Our null-guarded Input methods (see Input.hpp) return safe
 *    defaults when s_Instance is null.
 *
 * 2. Initialize the logging system so AYAYA_CORE_INFO etc. don't crash.
 */
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Input.hpp"

// Override Input singleton (must be nullptr for headless tests)
Ayaya::Input* Ayaya::Input::s_Instance = nullptr;

// Test environment initializer — called before main() by GTest
struct TestEnvironment {
    TestEnvironment() {
        Ayaya::Log::Init();
    }
};
static TestEnvironment s_TestEnv;
