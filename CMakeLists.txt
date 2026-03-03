cmake_minimum_required(VERSION 3.20)
project(AyayaEngine VERSION 0.1.0 LANGUAGES CXX C)

# 1. C++ 标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. 针对 macOS 的特定设置与 Frameworks 链接
if(APPLE)
    set(CMAKE_MACOSX_RPATH 1)
    # macOS 运行 OpenGL/GLFW 必须的系统级框架
    set(APPLE_FRAMEWORKS "-framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -framework QuartzCore")
endif()

# 3. 定义路径变量
set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(VENDOR_INCLUDE "${VENDOR_DIR}/include")
set(VENDOR_LIB "${VENDOR_DIR}/lib")

# 4. 定义引擎静态库 (更名为 Ayaya)
file(GLOB_RECURSE ENGINE_SOURCES 
    "src/Engine/*.cpp" 
    "src/Engine/*.hpp" 
    "src/Engine/*.c"
)

# 检查源文件
if(NOT ENGINE_SOURCES)
    message(FATAL_ERROR "No source files found in src/Engine! Ayaya needs code to breathe.")
endif()

add_library(Ayaya STATIC ${ENGINE_SOURCES})

# 强制将 glad.c 识别为 C 语言编译
set_source_files_properties(src/Engine/Renderer/glad.c PROPERTIES LANGUAGE C)

# 设置引擎的包含路径
# 由于 spdlog 在 vendor/include/spdlog，这里包含 vendor/include 即可
target_include_directories(Ayaya PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Engine"
    "${VENDOR_INCLUDE}"
)

# 5. 链接第三方库
find_library(GLFW_LIB NAMES glfw glfw3 PATHS ${VENDOR_LIB} NO_DEFAULT_PATH)

target_link_libraries(Ayaya PUBLIC 
    ${GLFW_LIB}
    ${APPLE_FRAMEWORKS}
)

# 6. 定义可执行文件 (Sandbox)
add_executable(Sandbox src/Sandbox/Main.cpp)

# 链接 Ayaya 引擎
# 注意：现在 Sandbox 链接的是 Ayaya 而不是 Engine
target_link_libraries(Sandbox PRIVATE Ayaya)

# 7. 资源文件处理
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:Sandbox>/assets
    COMMENT "Creating symlink to assets directory for Ayaya"
)