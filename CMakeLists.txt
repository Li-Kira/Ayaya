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

# 4. 收集底层引擎核心源文件 (Ayaya Library)
# 注意：SCENE_SOURCES 现在只会收集 Engine/Scene 里的文件，
# 我们的 SceneHierarchyPanel 已经被移到了 AyayaEditor/Panels 下，所以不会被编译进核心库里
file(GLOB CORE_SOURCES "${ENGINE_DIR}/Core/*.cpp")
file(GLOB RENDERER_SOURCES "${ENGINE_DIR}/Renderer/*.cpp")
file(GLOB OPENGL_SOURCES "${ENGINE_DIR}/Platform/OpenGL/*.cpp")
file(GLOB SCENE_SOURCES "${ENGINE_DIR}/Scene/*.cpp")
file(GLOB UTILS_SOURCES "${ENGINE_DIR}/Utils/*.cpp")
file(GLOB ASSET_SOURCES "${ENGINE_DIR}/Asset/*.cpp")
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
)

# 收集 ImGuizmo 源文件
set(IMGUIZMO_SOURCES
    "${IMGUIZMO_DIR}/ImGuizmo.cpp"
)

set(AYAYA_SOURCES 
    ${CORE_SOURCES} 
    ${RENDERER_SOURCES} 
    ${OPENGL_SOURCES}
    ${SCENE_SOURCES}
    ${UTILS_SOURCES}
    ${ASSET_SOURCES}
    ${GLAD_SOURCE}
    ${IMGUI_SOURCES} 
    ${IMGUIZMO_SOURCES} 
)

# 5. 定义引擎静态库 (Engine Core)
add_library(Ayaya STATIC ${AYAYA_SOURCES})
target_compile_definitions(Ayaya PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
set_source_files_properties(${GLAD_SOURCE} PROPERTIES LANGUAGE C)

# --- 配置预编译头 (PCH) ---
target_precompile_headers(Ayaya PRIVATE 
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/ayapch.h>"
)

# 6. 设置包含路径
find_path(ASSIMP_INCLUDE_DIR NAMES assimp/Importer.hpp PATHS /opt/homebrew/include /usr/local/include /usr/include)

target_include_directories(Ayaya PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/src" 
    "${ENGINE_DIR}"                   
    "${VENDOR_INCLUDE}"
    "${IMGUI_DIR}"           # 允许 #include <imgui.h>
    "${IMGUI_DIR}/backends"  # 允许 #include <imgui_impl_glfw.h>
    "${STB_DIR}"             # 允许 #include <stb_image.h>
    "${ENTT_DIR}"            # 允许包含 EnTT 头文件
    "${IMGUIZMO_DIR}"        # 允许直接 #include <ImGuizmo.h>
    "${YAML_CPP_DIR}/include" # <--- 新增：让引擎能找到 <yaml-cpp/yaml.h>
    ${ASSIMP_INCLUDE_DIR}    # <--- 新增：让引擎能找到 Assimp 的头文件
)

# 7. 链接第三方库
find_library(GLFW_LIB NAMES glfw glfw3 PATHS ${VENDOR_LIB} NO_DEFAULT_PATH)
find_library(ASSIMP_LIB NAMES assimp PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
target_link_libraries(Ayaya PUBLIC ${GLFW_LIB} ${APPLE_FRAMEWORKS} yaml-cpp ${ASSIMP_LIB})

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

# 复制资源到 Editor 的输出目录
add_custom_command(TARGET AyayaEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:AyayaEditor>/assets"
    COMMENT "Creating symlink to assets for AyayaEditor"
)

# =========================================================================
# 9. 定义 Sandbox (游戏运行端程序 - 面向玩家)
# =========================================================================
file(GLOB SANDBOX_SOURCES "src/Sandbox/*.cpp" "src/Sandbox/*.hpp")
add_executable(Sandbox ${SANDBOX_SOURCES})
target_link_libraries(Sandbox PRIVATE Ayaya)

# 复制资源到 Sandbox 的输出目录
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:Sandbox>/assets"
    COMMENT "Creating symlink to assets for Sandbox"
)