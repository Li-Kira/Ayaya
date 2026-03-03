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

# 4. 收集源文件 (按模块划分，结构更清晰)
# 核心层
file(GLOB CORE_SOURCES "${ENGINE_DIR}/Core/*.cpp")
# 渲染抽象层
file(GLOB RENDERER_SOURCES "${ENGINE_DIR}/Renderer/*.cpp")
# 平台实现层 (目前仅 OpenGL)
file(GLOB OPENGL_SOURCES "${ENGINE_DIR}/Platform/OpenGL/*.cpp")
# C 文件 (Glad)
set(GLAD_SOURCE "${ENGINE_DIR}/Platform/OpenGL/glad.c")

# 整合所有源文件
set(AYAYA_SOURCES 
    ${CORE_SOURCES} 
    ${RENDERER_SOURCES} 
    ${OPENGL_SOURCES}
    ${GLAD_SOURCE}
)

# 5. 定义引擎静态库
add_library(Ayaya STATIC ${AYAYA_SOURCES})


# 强制将 glad.c 识别为 C 语言
set_source_files_properties(${GLAD_SOURCE} PROPERTIES LANGUAGE C)

# 6. 设置包含路径
# PUBLIC 确保 Sandbox 也能自动获得这些包含路径
target_include_directories(Ayaya PUBLIC 
    "${ENGINE_DIR}"      # 允许 #include "Core/Log.hpp"
    "${VENDOR_INCLUDE}"   # 允许 #include <glm/glm.hpp>
)

# 7. 链接第三方库
find_library(GLFW_LIB NAMES glfw glfw3 PATHS ${VENDOR_LIB} NO_DEFAULT_PATH)

target_link_libraries(Ayaya PUBLIC 
    ${GLFW_LIB}
    ${APPLE_FRAMEWORKS}
)

# 8. 定义可执行文件 (Sandbox)
# 自动收集 Sandbox 目录下所有的源文件（包括 Main.cpp 和层文件）
file(GLOB SANDBOX_SOURCES "src/Sandbox/*.cpp" "src/Sandbox/*.hpp")
add_executable(Sandbox ${SANDBOX_SOURCES})
target_link_libraries(Sandbox PRIVATE Ayaya)

# 9. 资源文件处理 (创建符号链接)
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:Sandbox>/assets"
    COMMENT "Creating symlink to assets directory for Sandbox"
)