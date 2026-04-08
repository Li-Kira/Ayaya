#include "Application.hpp"
#include <GLFW/glfw3.h>
#include "Input.hpp"
#include "Log.hpp"
#include "KeyCodes.hpp"
#include "Renderer/Renderer.hpp" 
#include "Platform/Vulkan/VulkanContext.hpp"
#ifdef _WIN32
    #include <windows.h>
#endif

namespace Ayaya {

    Application* Application::s_Instance = nullptr;

    Application::Application() {
        s_Instance = this; 

#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        
        // 获取控制台句柄，开启虚拟终端转义序列处理
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
#endif
        
        Log::Init();
        
        const char* ayayaAscii = R"(   
"What starts with me also ends with me."
Petals and stars are dedicated to the hymns of love and eternity.
She descended from the stars and the moon to fulfill a beautiful tale in the mortal world. Her drops of joy and sadness glittered in the darkness and became strokes in the splendid new chapter of hope authored by her.
She is a human-like Herrscher, humanity's Herrscher.
Hi, welcome to Ayaya engine♪                                                                                  
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@**@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@::%@@@@@@@@@@@@%#+=----=+#%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@:++-@@@@@@@@@+::=*%%%%%%%#*+-.:=#@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@--@*:@@@@*-...*%**%%%%%%%%%@@@@@#-.-*@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@=:@@*:@#:.+%%%%***%%%%%%%%%%@@@@@@@@%=.:-#@@@@@@*=@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@:=@@@+:::%%%%%%%*#@%#%@%%%%%%%@@@@@@@@@@@@*-:..:+%:+@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@:*@@@#.:%%%%%%%%%%#*****#%%%%@@@@@@@@@@@@@@%%%%%%%#-=@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@:*--@-.+@%%%%%%##%%%%%%%%###%@@@@@@@@@@@@@@@@%%%%%##:+@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@*-+--.-#%%%%%%#*#%%%%%%%%%%%%#%@@@@@@@@@@@@@@@@%%%#*+:%@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@:*.%+-#%%%%%%#*%%%%%%%%%%*:%%%%#@@@@@%%%@@@@@@%%%%%*.#@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@%:-+@==%%%%%%%*%%%%%%%%%@@%::%%%%#%%%%%%%%%%%%%%%%%%%.-@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@#::@@.*%%%%%%*%%%%%%@@@@%%@:%.*@%%*%%%%%%%#%@##%%%%%%%:=@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@#.#:*@--@%%%%%##@@%%%%%%%%%%@:%@=.%%%*%%%%%%##@@@=*@%%%%*:=@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@=-#:%.-@%%%%%%#@*+%%%%%%%%%%%.@@@@-:#@#%%%%%#*@@@@%=#%%%#=:%@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@*:+..=%%%%%%%##%.-%%%%%%%%#%*-@@@@@@+::-@%%%#+@@@@@@#=#%%#::@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@::-#%%%%%%%%*%-:=@%%%%%%##%.#@@@=.:--+:*@@%%+%@@@@@@@++@#+.%@@@@@@@@@@@@@@@@
@@@@@@@@@@@%%@@@@=.+%%%%%%%%%#-=:#%%%%%%+*#=-@*.:::.::::::.#@=#%@@@@@@@#=#*.=@@@@@@@@@@@@@@@@
@@@@@@@@%%%%%@@@+=.#%%%%%%%%@==#:%%%%@#..*--@.==:.*@-..%:-@%@**@##@@@@@@%=*-:@@@@@@@@@@@@@@@@
@@@@@@@@@%%%%%@*+=:%%%%%%%%%%-=:::::.=%%..+@@@@+=*#@@**@:*@%%%=@@#+@@@@@@@=-:@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@%+@:-@%%%%%%%%@=::::-:.%@@%@@@@@@@*%%%%%%@-+@%%@*+@@@=@@@@@@@=:%@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@+#@--%%%%%%%%=.:-=::@@++@@@@@@@@@@@%%%%%%@:+@%%%%+*@@@*#@@@@@@:#@@@@@@@@@@@@@@@
@@@@@@@@@@@@@*+@@=:%#%%%%%%*:..#+*#@%#@@@@@@@@@@@@%%%%%@:*%%%%%@+*@@@++%@@@@+:@@@@@@@@@@@@@@@
@@@@@@@@@@@@%=@@@#.#*%%%%%%%%@--@%%%%@@@@@%**+#@@@%%%%@*.#%#%%%%@*=@@+%@=*@@@:+@@@@@@@@@@@@@@
@@@@@@@@@@@@+#@@@@:+##%%%%%%%%%:=@%@%%@@@#****#@@@@@@@-:-%*%%%%@@@@+=+#@@#..%+-@@@@@@@@@@@@@@
@@@@@@@@@@@%+#@@@@=:#-+@%%%%%%%%::@-:..::@@#**@@@@@#:--:#=:@%%@@@@@#@%#@#.*%::-@@@@@@@@@@@@@@
@@@@@@@@@@@@@#++%@@.==.+@%%%%%#%@@-=+-#@%+=:=++@-.:-=:::::%%@@@@*:::+@@-:+@@@:-@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@#++-:=::-@%%%%@@#==:==@@@:%@-=+:@%-::::-.*%@@%-.=++.+=.=:#@@@@@@@@@%@@@@@@@@@
@@@@@@+@@@@@@@@@@@@@=::::.=@%%@:@#=@@*-@%:.-:++:@@%*@@@%.#@@:.=++++..+#-=@@@##########@@@@@@@
@@@@@@-:#@@@@@@@@@@@@-::*#=-*@=:@@#@@@=..#@-.-::::+@@@@#.%#.-++++=*#*#+.@@@@##########@@@@@@@
@@@@@@#:*::-+**++-:::-#***%*.+=.%%#@@@+.-**@@%:*.%-*::--.=.=++++++#*#+:@@@@@@########@@@@@@@@
@@@@@@@*:###*++++*##******#%.@%=.++@@-==*#@@@#::@-=+-@@@::-++++++**#=:%@@@@@%#########%@@@@@@
@@@@@@@@#:-*#**************%#:%+++=:.*=::+%@%::%--=--*%@-.=+++++*##-:@@@@@@@##########%@@@@@@
@@@@@@@@@@#:::=+***--#***@=.:==:--:.=-.::::::*==%%%:@@@@:=++++++*#+:@@@@@@@@@%%%@@@@@@@@@@@@@
@@@@@@@@@@@@@@*==.=#**#+===+*@#..-:@@@-+#%%+.+%@@@@%-@--+++++++*#+:@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@:-#*#=.:+++=#@.#%%%+..-:=+#%:*@%:@@@%.#*:+++++**#--@@@@@@@@%@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@+:*#*:-%:=+++==##:#+-::@@@@-::#@%#:@%-:+:%.++=:*#*.+@@@@@@@@%#%%%%@@@@@@@@@@@@@@
@@@@@@@@@@@@--+-.=@@#::-+*#%%%@*.%*:@@@@@@@@@%::.==.-:*.:.+#*:-@@@@@@@@@%%%%%%@@@@@@@@@@@@@@@
@@@@@@@@@@@+=*#%@@@@@@@@@@@@@@@@#::-:+++-:::=#@+--:=:-:::--:-%@@@@@@@@@@@%@%%%@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%#*-::#@@@::%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
        )";

        AYAYA_CORE_INFO("\x1b[38;2;255;150;200mAyaya Engine is starting up... ♪\n{0}\x1b[0m", ayayaAscii);
        AYAYA_CORE_INFO("Log System Initialized!");

        m_Window = std::make_unique<Window>(1920, 1080, "Ayaya Engine v0.1");
        m_Window->SetEventCallback(std::bind(&Application::OnEvent, this, std::placeholders::_1));
        AYAYA_CORE_INFO("GLFW Window initialized successfully.");

        // 【核心防御】：只有在 OpenGL 模式下才初始化旧的 3D 渲染器
        if (Renderer::GetAPI() == RendererAPI::API::OpenGL) {
            Renderer::Init();
            AYAYA_CORE_INFO("Renderer initialized successfully.");
        } else {
            AYAYA_CORE_INFO("Vulkan 3D Renderer is under construction, bypassed OpenGL Renderer::Init().");
        }

        // 创建并初始化 ImGuiLayer
        m_ImGuiLayer = new ImGuiLayer();
        PushOverlay(m_ImGuiLayer);
        AYAYA_CORE_INFO("ImGui initialized successfully.");
    }

    Application::~Application() {
        // 【核心修复】：安全清理
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            Renderer::Shutdown();
        }
    }

    void Application::PushLayer(Layer* layer) { 
        m_LayerStack.PushLayer(layer); 
        layer->OnAttach(); 
    }
    
    void Application::PushOverlay(Layer* overlay) { 
        m_LayerStack.PushOverlay(overlay); 
        overlay->OnAttach(); 
    }

    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        
        dispatcher.Dispatch<WindowCloseEvent>(std::bind(&Application::OnWindowClose, this, std::placeholders::_1));
        dispatcher.Dispatch<WindowResizeEvent>(std::bind(&Application::OnWindowResize, this, std::placeholders::_1));
        dispatcher.Dispatch<KeyPressedEvent>(std::bind(&Application::OnKeyPressed, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseButtonPressedEvent>(std::bind(&Application::OnMouseButtonPressed, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseMovedEvent>(std::bind(&Application::OnMouseMoved, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseScrolledEvent>(std::bind(&Application::OnMouseScrolled, this, std::placeholders::_1));

        // 如果 ImGui 拦截了鼠标，e.Handled 会变为 true，后续 Layer 不再处理
        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it) {
            if (e.Handled)
                break;
            (*it)->OnEvent(e);
        }
    }

    void Application::Run() {
        while (m_Running) {
            float time = (float)glfwGetTime(); 
            Timestep timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            // 1. 逻辑更新（渲染场景）
            for (Layer* layer : m_LayerStack)
                layer->OnUpdate(timestep);

            // 2. ImGui 渲染阶段
            m_ImGuiLayer->Begin();
            for (Layer* layer : m_LayerStack)
                layer->OnImGuiRender(); // 每个层渲染自己的 UI
            m_ImGuiLayer->End();

            m_Window->OnUpdate();
        }
    }

    // =========================================================================
    // 事件处理函数实现
    // =========================================================================
    
    bool Application::OnWindowClose(WindowCloseEvent& e) {
        m_Running = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e) {
        if (e.GetWidth() == 0 || e.GetHeight() == 0) {
            return false; // 防止窗口最小化时触发 0x0 渲染崩溃
        }
        
        AYAYA_CORE_INFO("Window Resize Logic: {0}, {1}", e.GetWidth(), e.GetHeight());
        
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());
        } 
        else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            // ==========================================
            // 【核心修复】：如果是 Vulkan 模式，一旦触发 Resize 或 Retina 缩放，
            // 立即命令底层重建所有画布！
            // ==========================================
            auto vulkanContext = std::dynamic_pointer_cast<VulkanContext>(m_Window->GetContext());
            vulkanContext->RecreateSwapChain();
        }
        return false;
    }

    // 实现按键处理逻辑
    bool Application::OnKeyPressed(KeyPressedEvent& e) {
        if (e.GetKeyCode() == Key::Escape) {
            AYAYA_CORE_INFO("Escape key pressed!");
            m_Running = false;
            return true;
        }

        if (e.GetKeyCode() == Key::Enter) {
            AYAYA_CORE_INFO("Enter key pressed! [Verified by Event System]");
            return true;
        }

        return false;
    }

    bool Application::OnMouseButtonPressed(MouseButtonPressedEvent& e) {
        AYAYA_CORE_TRACE("Mouse Button Pressed: {0}", e.GetMouseButton());
        float mouseX = Input::GetMouseX();
        float mouseY = Input::GetMouseY();
        
        AYAYA_CORE_TRACE("Mouse Button Pressed: {0} at position ({1}, {2})", e.GetMouseButton(), mouseX, mouseY);
        return false; // 返回 false 以允许事件继续传递给 Layer
    }

    bool Application::OnMouseMoved(MouseMovedEvent& e) {
        // 注意：鼠标移动较频繁，生产环境建议按需开启
        // AYAYA_CORE_TRACE("Mouse Moved: {0}, {1}", e.GetX(), e.GetY());
        return false;
    }

    bool Application::OnMouseScrolled(MouseScrolledEvent& e) {
        AYAYA_CORE_TRACE("Mouse Scrolled: {0}, {1}", e.GetXOffset(), e.GetYOffset());
        return false;
    }
}