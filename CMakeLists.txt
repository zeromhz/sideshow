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
)
target_include_directories(sideshow_core PUBLIC src)
target_compile_options(sideshow_core PRIVATE -Wall -Wextra)
target_link_libraries(sideshow_core PUBLIC tomlplusplus::tomlplusplus)

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
    )
    target_link_libraries(sideshow_tests PRIVATE sideshow_core Catch2::Catch2WithMain)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(sideshow_tests)
endif()
