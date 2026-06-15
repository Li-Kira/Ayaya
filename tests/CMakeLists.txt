# =========================================================================
# Ayaya Engine Unit Tests
# =========================================================================

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        release-1.12.1
)
# Prevent gtest from building its own samples/tests, and suppress warnings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- Collect test source files ---
file(GLOB CORE_TEST_SOURCES     "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.cpp")
file(GLOB EVENTS_TEST_SOURCES   "${CMAKE_CURRENT_SOURCE_DIR}/Events/*.cpp")
file(GLOB SCENE_TEST_SOURCES    "${CMAKE_CURRENT_SOURCE_DIR}/Scene/*.cpp")
file(GLOB SCRIPTING_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Scripting/*.cpp")

set(TEST_SOURCES
    ${CORE_TEST_SOURCES}
    ${EVENTS_TEST_SOURCES}
    ${SCENE_TEST_SOURCES}
    ${SCRIPTING_TEST_SOURCES}
)

# --- Test executable ---
add_executable(AyayaTests ${TEST_SOURCES})

target_link_libraries(AyayaTests PRIVATE Ayaya gtest_main)

# --- Include paths (mirror what Ayaya target exposes) ---
target_include_directories(AyayaTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/Engine"
    "${CMAKE_SOURCE_DIR}/vendor/include"
    "${CMAKE_SOURCE_DIR}/vendor/include/imgui"
    "${CMAKE_SOURCE_DIR}/vendor/include/imgui/backends"
    "${CMAKE_SOURCE_DIR}/vendor/include/stb"
    "${CMAKE_SOURCE_DIR}/vendor/include/entt"
    "${CMAKE_SOURCE_DIR}/vendor/include/ImGuizmo"
    "${CMAKE_SOURCE_DIR}/vendor/include/sol"
    "${CMAKE_SOURCE_DIR}/vendor/lua"
    "${CMAKE_SOURCE_DIR}/vendor/lua/src"
    "${CMAKE_SOURCE_DIR}/vendor/yaml-cpp/include"
    "${CMAKE_SOURCE_DIR}/vendor/include/vma"
)

# Suppress third-party warnings on MSVC
if(MSVC)
    target_compile_options(AyayaTests PRIVATE /W3 /wd4100 /wd4127 /wd4244 /wd4267 /wd4389 /wd4702)
endif()

# --- Copy DLLs on Windows so the test executable can run ---
if(WIN32)
    file(GLOB VENDOR_DLLS "${CMAKE_SOURCE_DIR}/vendor/bin/*.dll")
    if(VENDOR_DLLS)
        add_custom_command(TARGET AyayaTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${VENDOR_DLLS}
            "$<TARGET_FILE_DIR:AyayaTests>"
            COMMENT "Auto-copying DLLs to AyayaTests output directory..."
        )
    endif()
endif()

# Register with CTest
enable_testing()
include(GoogleTest)
gtest_discover_tests(AyayaTests)
