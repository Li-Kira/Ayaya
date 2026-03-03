cmake_minimum_required(VERSION 3.20)
project(AyayaEngine VERSION 0.1.0 LANGUAGES CXX C)

# 1. C++ 标准设置
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
# 新增 ImGui 路径
set(IMGUI_DIR "${VENDOR_DIR}/include/imgui")

# 4. 收集源文件
file(GLOB CORE_SOURCES "${ENGINE_DIR}/Core/*.cpp")
file(GLOB RENDERER_SOURCES "${ENGINE_DIR}/Renderer/*.cpp")
file(GLOB OPENGL_SOURCES "${ENGINE_DIR}/Platform/OpenGL/*.cpp")
set(GLAD_SOURCE "${ENGINE_DIR}/Platform/OpenGL/glad.c")

# --- 新增：收集 ImGui 源文件 ---
set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

set(AYAYA_SOURCES 
    ${CORE_SOURCES} 
    ${RENDERER_SOURCES} 
    ${OPENGL_SOURCES}
    ${GLAD_SOURCE}
    ${IMGUI_SOURCES} # 加入 ImGui
)

# 5. 定义引擎静态库
add_library(Ayaya STATIC ${AYAYA_SOURCES})
target_compile_definitions(Ayaya PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
set_source_files_properties(${GLAD_SOURCE} PROPERTIES LANGUAGE C)

# --- 配置预编译头 (PCH) ---
target_precompile_headers(Ayaya PRIVATE 
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/ayapch.h>"
)

# 6. 设置包含路径
target_include_directories(Ayaya PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/src" 
    "${ENGINE_DIR}"                   
    "${VENDOR_INCLUDE}"
    "${IMGUI_DIR}"           # 允许 #include <imgui.h>
    "${IMGUI_DIR}/backends"  # 允许 #include <imgui_impl_glfw.h>
)

# 7. 链接第三方库
find_library(GLFW_LIB NAMES glfw glfw3 PATHS ${VENDOR_LIB} NO_DEFAULT_PATH)
target_link_libraries(Ayaya PUBLIC ${GLFW_LIB} ${APPLE_FRAMEWORKS})

# 8. 定义 Sandbox
file(GLOB SANDBOX_SOURCES "src/Sandbox/*.cpp" "src/Sandbox/*.hpp")
add_executable(Sandbox ${SANDBOX_SOURCES})
target_link_libraries(Sandbox PRIVATE Ayaya)

# 9. 资源文件处理
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:Sandbox>/assets"
    COMMENT "Creating symlink to assets directory"
)