cmake_minimum_required(VERSION 3.20)
project(AyayaEngine VERSION 0.1.0 LANGUAGES CXX C)

# 1. C++ 标准设置 (EnTT 强依赖 C++17 或更高版本，这里已经满足)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. 平台特定设置
if(APPLE)
    set(CMAKE_MACOSX_RPATH 1)
    set(APPLE_FRAMEWORKS "-framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -framework QuartzCore")
    add_definitions(-DGLM_FORCE_DEPTH_ZERO_TO_ONE) 
elseif(WIN32)
    # Windows 平台特有设置
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    
    # ==========================================
    # 【核心修复】：强制 MSVC 编译器使用 UTF-8 编码！
    # 解决 spdlog 的 static_assert 报错和 C4819 中文乱码警告
    # ==========================================
    add_compile_options(/utf-8) 
    
    set(OPENGL_LIB opengl32) # Windows 自带的 OpenGL 库
endif()

# 3. 定义路径变量
set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/Engine")
set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(VENDOR_INCLUDE "${VENDOR_DIR}/include")
set(VENDOR_LIB "${VENDOR_DIR}/lib")
set(YAML_CPP_DIR "${VENDOR_DIR}/yaml-cpp") # <--- 新增 yaml-cpp 路径

# 强制关闭 yaml-cpp 自带的测试、工具和额外组件的编译，只编译核心库！
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "Disable yaml-cpp contrib" FORCE)

add_subdirectory(${YAML_CPP_DIR})

# ImGui 路径
set(IMGUI_DIR "${VENDOR_DIR}/include/imgui")
# EnTT 路径
set(ENTT_DIR "${VENDOR_INCLUDE}/entt")
# ImGuizmo 路径
set(IMGUIZMO_DIR "${VENDOR_INCLUDE}/ImGuizmo")
# stb 路径
set(STB_DIR "${VENDOR_INCLUDE}/stb")
# ==========================================
# 新增：配置 Box2D 物理引擎
# ==========================================
set(BOX2D_DIR "${VENDOR_INCLUDE}/Box2D")
# 强制关闭 Box2D 的测试床和文档，只编译核心物理库
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "Disable Box2D Testbed" FORCE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "Disable Box2D Unit Tests" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "Disable Box2D Docs" FORCE)
add_subdirectory(${BOX2D_DIR})

# ==========================================
# 【新增】：寻找本地安装的 Vulkan SDK
# ==========================================
find_package(Vulkan REQUIRED)
if (Vulkan_FOUND)
    message(STATUS "Vulkan SDK Found: ${Vulkan_LIBRARY}")
else()
    message(FATAL_ERROR "Vulkan SDK NOT FOUND! Please install LunarG Vulkan SDK.")
endif()
add_definitions(-DGLFW_INCLUDE_NONE -DGLFW_INCLUDE_VULKAN)

# 4. 收集底层引擎核心源文件 (Ayaya Library)
# 注意：SCENE_SOURCES 现在只会收集 Engine/Scene 里的文件，
# 我们的 SceneHierarchyPanel 已经被移到了 AyayaEditor/Panels 下，所以不会被编译进核心库里
file(GLOB CORE_SOURCES "${ENGINE_DIR}/Core/*.cpp")
file(GLOB RENDERER_SOURCES "${ENGINE_DIR}/Renderer/*.cpp")
file(GLOB RENDERER_PASSES_SOURCES "${ENGINE_DIR}/Renderer/Passes/*.cpp")
file(GLOB OPENGL_SOURCES "${ENGINE_DIR}/Platform/OpenGL/*.cpp")
file(GLOB VULKAN_SOURCES "${ENGINE_DIR}/Platform/Vulkan/*.cpp")
file(GLOB SCENE_SOURCES "${ENGINE_DIR}/Scene/*.cpp")
file(GLOB SCRIPTING_SOURCES "${ENGINE_DIR}/Scripting/*.cpp")
file(GLOB UTILS_SOURCES "${ENGINE_DIR}/Utils/*.cpp")
file(GLOB ASSET_SOURCES "${ENGINE_DIR}/Asset/*.cpp")
file(GLOB PROJECT_SOURCES "${ENGINE_DIR}/Project/*.cpp")
set(GLAD_SOURCE "${ENGINE_DIR}/Platform/OpenGL/glad.c")

# 收集 ImGui 源文件
set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
)

# 收集 ImGuizmo 源文件
set(IMGUIZMO_SOURCES
    "${IMGUIZMO_DIR}/ImGuizmo.cpp"
)

set(AYAYA_SOURCES 
    ${CORE_SOURCES} 
    ${RENDERER_SOURCES} 
    ${RENDERER_PASSES_SOURCES} 
    ${OPENGL_SOURCES}
    ${VULKAN_SOURCES}
    ${SCENE_SOURCES}
    ${SCRIPTING_SOURCES}
    ${UTILS_SOURCES}
    ${ASSET_SOURCES}
    ${PROJECT_SOURCES}
    ${GLAD_SOURCE}
    ${IMGUI_SOURCES} 
    ${IMGUIZMO_SOURCES} 
)

# 5. 定义引擎静态库 (Engine Core)
add_library(Ayaya STATIC ${AYAYA_SOURCES})
target_compile_definitions(Ayaya PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
set_source_files_properties(${GLAD_SOURCE} PROPERTIES LANGUAGE C)

# 编译lua
file(GLOB LUA_SOURCES "${VENDOR_DIR}/lua/*.c" "${VENDOR_DIR}/lua/src/*.c")

if(LUA_SOURCES)
    list(FILTER LUA_SOURCES EXCLUDE REGEX "lua\\.c$")
    list(FILTER LUA_SOURCES EXCLUDE REGEX "luac\\.c$")
else()
    message(FATAL_ERROR "Could not find Lua source files! Please check your vendor/lua directory.")
endif()

add_library(Lua STATIC ${LUA_SOURCES})

# --- 配置预编译头 (PCH) ---
target_precompile_headers(Ayaya PRIVATE 
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/ayapch.h>"
)

# 6. 设置包含路径
# 增加了 ${VENDOR_INCLUDE}，让 Windows 和 Mac 都能在 vendor 目录下找到 Assimp
find_path(ASSIMP_INCLUDE_DIR NAMES assimp/Importer.hpp PATHS "${VENDOR_INCLUDE}" /opt/homebrew/include /usr/local/include /usr/include)

target_include_directories(Ayaya PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/src" 
    "${ENGINE_DIR}"                   
    "${VENDOR_INCLUDE}"
    "${IMGUI_DIR}"           # 允许 #include <imgui.h>
    "${IMGUI_DIR}/backends"  # 允许 #include <imgui_impl_glfw.h>
    "${STB_DIR}"             # 允许 #include <stb_image.h>
    "${ENTT_DIR}"            # 允许包含 EnTT 头文件
    "${IMGUIZMO_DIR}"        # 允许直接 #include <ImGuizmo.h>
    "${YAML_CPP_DIR}/include" # 允许引擎能找到 <yaml-cpp/yaml.h>
    ${ASSIMP_INCLUDE_DIR}    # 允许引擎能找到 Assimp 的头文件
    "${BOX2D_DIR}/include"
    "${VENDOR_INCLUDE}/sol"
    "${VENDOR_DIR}/lua"
    "${VENDOR_DIR}/lua/src"

    # 【新增】：Vulkan SDK 的头文件路径
    ${Vulkan_INCLUDE_DIRS}
    "${VENDOR_INCLUDE}/vma"
)

# 7. 链接第三方库
find_library(GLFW_LIB NAMES glfw glfw3 PATHS "${VENDOR_LIB}" NO_DEFAULT_PATH)
# 增加了 Windows 平台常见的 Assimp 静态库名 (assimp-vc143-mt 等) 和 vendor 路径
find_library(ASSIMP_LIB NAMES assimp assimp-vc143-mt assimp-vc142-mt PATHS "${VENDOR_LIB}" /opt/homebrew/lib /usr/local/lib /usr/lib)

if(APPLE)
    # Mac 平台链接
    target_link_libraries(Ayaya PUBLIC ${GLFW_LIB} ${APPLE_FRAMEWORKS} yaml-cpp ${ASSIMP_LIB} box2d Lua Vulkan::Vulkan)
elseif(WIN32)
    # Windows 平台链接
    target_link_libraries(Ayaya PUBLIC ${GLFW_LIB} ${OPENGL_LIB} yaml-cpp ${ASSIMP_LIB} box2d Lua Vulkan::Vulkan)
endif()


# =========================================================================
# 8. 定义 AyayaEditor (编辑器应用程序 - 面向开发者)
# =========================================================================
file(GLOB EDITOR_SOURCES 
    "src/AyayaEditor/*.cpp" 
    "src/AyayaEditor/*.hpp"
    "src/AyayaEditor/Panels/*.cpp"  # 确保面板代码被编译进编辑器
    "src/AyayaEditor/Panels/*.hpp"
)
add_executable(AyayaEditor ${EDITOR_SOURCES})
target_link_libraries(AyayaEditor PRIVATE Ayaya)


# =========================================================================
# 9. 定义 Sandbox (游戏运行端程序 - 面向玩家)
# =========================================================================
file(GLOB SANDBOX_SOURCES "src/Sandbox/*.cpp" "src/Sandbox/*.hpp")
add_executable(Sandbox ${SANDBOX_SOURCES})
target_link_libraries(Sandbox PRIVATE Ayaya)


# =========================================================================
# 10. 智能跨平台资源复制逻辑
# =========================================================================
if(WIN32)
    # Windows 下如果没有开发者模式权限，软链接会报错，这里改用直接复制文件夹
    set(COPY_COMMAND copy_directory)
    set(COPY_COMMENT "Copying assets directory for")
else()
    # Mac/Linux 下使用软链接，保持同步和节省空间
    set(COPY_COMMAND create_symlink)
    set(COPY_COMMENT "Creating symlink to assets for")
endif()

# 为 Editor 复制/链接资源
add_custom_command(TARGET AyayaEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E ${COPY_COMMAND}
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:AyayaEditor>/assets"
    COMMENT "${COPY_COMMENT} AyayaEditor"
)

# 为 Sandbox 复制/链接资源
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E ${COPY_COMMAND}
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:Sandbox>/assets"
    COMMENT "${COPY_COMMENT} Sandbox"
)

# =========================================================================
# 11. 自动化 DLL 拷贝逻辑 (Windows 专属)
# =========================================================================
if(WIN32)
    # 自动搜集 vendor/bin 目录下的所有 .dll 文件
    file(GLOB VENDOR_DLLS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/bin/*.dll")
    
    if(VENDOR_DLLS)
        # 1. 为 AyayaEditor 自动拷贝 DLL
        add_custom_command(TARGET AyayaEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${VENDOR_DLLS} 
            "$<TARGET_FILE_DIR:AyayaEditor>"
            COMMENT "Auto-copying DLLs to AyayaEditor output directory..."
        )

        # 2. 为 Sandbox 自动拷贝 DLL
        add_custom_command(TARGET Sandbox POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${VENDOR_DLLS} 
            "$<TARGET_FILE_DIR:Sandbox>"
            COMMENT "Auto-copying DLLs to Sandbox output directory..."
        )
    endif()
endif()