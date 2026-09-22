cmake_minimum_required(VERSION 3.20)
project(sideshow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SIDESHOW_BUILD_PLAYER "Build sideshow-player (needs projectM, SDL2, liblo)" OFF)
option(SIDESHOW_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)

FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tomlplusplus)

# Pure logic with no GPU/audio/network dependencies -- unit tested on any machine.
add_library(sideshow_core STATIC
    src/output_level.cpp
    src/osc_parse.cpp
    src/audio_ring.cpp
    src/config.cpp
    src/preset_catalog.cpp
    src/display_mode.cpp
)
target_include_directories(sideshow_core PUBLIC src)
target_compile_options(sideshow_core PRIVATE -Wall -Wextra)
target_link_libraries(sideshow_core PUBLIC tomlplusplus::tomlplusplus)

if(SIDESHOW_BUILD_PLAYER)
    find_package(SDL2 REQUIRED)
    find_package(Threads REQUIRED)
    find_package(PkgConfig REQUIRED)
    find_package(projectM4 REQUIRED)
    find_package(projectM4Playlist REQUIRED)

    if(APPLE)
        find_package(OpenGL REQUIRED)
        set(SIDESHOW_GL_LIBS OpenGL::GL)
        set(SIDESHOW_GL_DEFS "")
    else()
        pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
        set(SIDESHOW_GL_LIBS PkgConfig::GLES)
        set(SIDESHOW_GL_DEFS SIDESHOW_GLES)
    endif()

    add_executable(sideshow-player
        src/main.cpp
        src/display.cpp
        src/brightness_pass.cpp
        src/renderer.cpp
        src/audio_capture.cpp
        src/preset_manager.cpp
    )
    target_compile_definitions(sideshow-player PRIVATE ${SIDESHOW_GL_DEFS})
    target_compile_options(sideshow-player PRIVATE -Wall -Wextra)
    target_link_libraries(sideshow-player PRIVATE
        sideshow_core SDL2::SDL2 Threads::Threads ${SIDESHOW_GL_LIBS}
        libprojectM::projectM libprojectM::playlist)
endif()

if(SIDESHOW_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    add_executable(sideshow_tests
        tests/test_output_level.cpp
        tests/test_command_queue.cpp
        tests/test_osc_parse.cpp
        tests/test_audio_ring.cpp
        tests/test_config.cpp
        tests/test_preset_catalog.cpp
        tests/test_display_mode.cpp
    )
    target_link_libraries(sideshow_tests PRIVATE sideshow_core Catch2::Catch2WithMain)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(sideshow_tests)
endif()
