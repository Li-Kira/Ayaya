cmake_minimum_required(VERSION 3.20)
project(Ayaya VERSION 0.1.0 LANGUAGES CXX C)

# 1. C++ 标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. 针对 macOS 的特定设置与 Frameworks 链接
if(APPLE)
    set(CMAKE_MACOSX_RPATH 1)
    # macOS 运行 OpenGL/GLFW 必须的系统级框架
    set(APPLE_FRAMEWORKS "-framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -framework QuartzCore")
endif()

# 3. 定义路径变量（方便维护）
set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(VENDOR_INCLUDE "${VENDOR_DIR}/include")
set(VENDOR_LIB "${VENDOR_DIR}/lib")

# 4. 定义引擎静态库 (Engine)
file(GLOB_RECURSE ENGINE_SOURCES 
    "src/Engine/*.cpp" 
    "src/Engine/*.hpp" 
    "src/Engine/*.c"
)

# 检查是否找到了源文件，防止 add_library 报错
if(NOT ENGINE_SOURCES)
    message(FATAL_ERROR "No source files found in src/Engine! Please add some .cpp files.")
endif()

add_library(Engine STATIC ${ENGINE_SOURCES})

# 设置引擎的包含路径
target_include_directories(Engine PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Engine"
    ${VENDOR_INCLUDE}
)

# 5. 链接第三方库到 Engine
# 注意：这里链接的是你 vendor/lib 下的具体文件
find_library(GLFW_LIB NAMES glfw glfw3 PATHS ${VENDOR_LIB} NO_DEFAULT_PATH)

target_link_libraries(Engine PUBLIC 
    ${GLFW_LIB}
    ${APPLE_FRAMEWORKS}
)

# 6. 定义可执行文件 (Sandbox)
add_executable(Sandbox src/Sandbox/Main.cpp)

# 如果你使用了 GLAD，需要把 glad.c 也加入编译（通常 glad 会提供一个 .c 文件）
# 如果你只有 glad.h，确保你的 Main.cpp 或相关文件里包含了 glad 的实现
target_link_libraries(Sandbox PRIVATE Engine)

# 7. 资源文件处理：创建一个指向 assets 的符号链接到编译目录
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:Sandbox>/assets
    COMMENT "Creating symlink to assets directory"
)