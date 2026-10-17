cmake_minimum_required(VERSION 3.20)
project(sideshow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SIDESHOW_BUILD_PLAYER "Build sideshow-player (needs projectM, SDL2, liblo)" OFF)
option(SIDESHOW_BUILD_GL_TESTS "Build optional real-context visualization tests" OFF)
option(SIDESHOW_BUILD_VIDEO_TESTS "Build FFmpeg decoder and worker tests" OFF)
option(SIDESHOW_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)
include(cmake/IsfSource.cmake)

FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tomlplusplus)

# Pure logic with no GPU/audio/network dependencies -- unit tested on any machine.
add_library(sideshow_core STATIC
    src/isf_playback.cpp
    src/isf_budget.cpp
    src/isf_images.cpp
    src/isf_loader.cpp
    src/isf_parameters.cpp
    src/isf_audio.cpp
    src/isf_fft.cpp
    src/isf_document.cpp
    src/isf_catalog.cpp
    src/isf_files.cpp
    src/video_playback.cpp
    src/video_types.cpp
    src/video_catalog.cpp
    src/visual_controller.cpp
    src/visual_mode.cpp
    src/fluid_playback.cpp
    src/fluid_motion.cpp
    src/fluid_presets.cpp
    src/output_level.cpp
    src/osc_parse.cpp
    src/audio_ring.cpp
    src/config.cpp
    src/preset_catalog.cpp
    src/display_mode.cpp
)
target_include_directories(sideshow_core PUBLIC src)
target_include_directories(sideshow_core SYSTEM PRIVATE ${vvisf_SOURCE_DIR}/VVISF/include ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
find_package(Threads REQUIRED)
target_link_libraries(sideshow_core PUBLIC Threads::Threads)
target_compile_options(sideshow_core PRIVATE -Wall -Wextra)
target_link_libraries(sideshow_core PUBLIC tomlplusplus::tomlplusplus)

if(SIDESHOW_BUILD_PLAYER OR SIDESHOW_BUILD_VIDEO_TESTS)
    find_package(PkgConfig REQUIRED)
    find_package(Threads REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat>=59.27 libavcodec>=59.37 libavutil>=57.28 libswscale>=6.7)
    add_library(sideshow_video STATIC src/video_decoder.cpp src/video_worker.cpp)
    target_link_libraries(sideshow_video PUBLIC sideshow_core PkgConfig::FFMPEG Threads::Threads)
    target_compile_options(sideshow_video PRIVATE -Wall -Wextra)
endif()

if(SIDESHOW_BUILD_PLAYER)
    find_package(SDL2 REQUIRED)
    find_package(Threads REQUIRED)
    find_package(PkgConfig REQUIRED)
    find_package(projectM4 REQUIRED)
    find_package(projectM4Playlist REQUIRED)
    pkg_check_modules(LIBLO REQUIRED IMPORTED_TARGET liblo)

    if(APPLE)
        find_package(OpenGL REQUIRED)
        set(SIDESHOW_GL_LIBS OpenGL::GL)
        set(SIDESHOW_GL_DEFS "")
    else()
        pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
        set(SIDESHOW_GL_LIBS PkgConfig::GLES)
        set(SIDESHOW_GL_DEFS SIDESHOW_GLES)
    endif()

    add_library(sideshow_fluid STATIC src/fluid_gl.cpp src/fluid_shaders.cpp src/fluid_renderer.cpp)
    target_compile_definitions(sideshow_fluid PUBLIC ${SIDESHOW_GL_DEFS})
    target_compile_options(sideshow_fluid PRIVATE -Wall -Wextra)
    target_link_libraries(sideshow_fluid PUBLIC sideshow_core ${SIDESHOW_GL_LIBS})
    add_library(sideshow_backends STATIC src/video_presenter.cpp src/video_backend.cpp src/fluid_backend.cpp src/milkdrop_backend.cpp
        src/renderer.cpp src/preset_manager.cpp src/brightness_pass.cpp)
    target_compile_options(sideshow_backends PRIVATE -Wall -Wextra)
    target_link_libraries(sideshow_backends PUBLIC sideshow_fluid sideshow_video libprojectM::projectM libprojectM::playlist)
    if(SIDESHOW_BUILD_GL_TESTS)
        add_executable(sideshow_video_gl_tests tests/test_video_gl.cpp src/video_presenter.cpp src/fluid_gl.cpp)
        target_compile_options(sideshow_video_gl_tests PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/video_gl_tracking.h)
        target_compile_definitions(sideshow_video_gl_tests PRIVATE VIDEO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/video" ${SIDESHOW_GL_DEFS})
        target_link_libraries(sideshow_video_gl_tests PRIVATE sideshow_backends sideshow_video SDL2::SDL2 ${SIDESHOW_GL_LIBS})
        add_executable(sideshow_gl_tests tests/test_visual_gl.cpp src/fluid_gl.cpp)
        # Link the instrumented GL ownership boundary in this executable only;
        # the archive's ordinary fluid_gl object is not extracted by the linker.
        target_compile_options(sideshow_gl_tests PRIVATE
            -include ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/fluid_gl_tracking.h)
        target_compile_definitions(sideshow_gl_tests PRIVATE ${SIDESHOW_GL_DEFS})
        target_link_libraries(sideshow_gl_tests PRIVATE sideshow_backends SDL2::SDL2 ${SIDESHOW_GL_LIBS})
    endif()
    add_executable(sideshow-player
        src/main.cpp
        src/display.cpp
        src/audio_capture.cpp
        src/osc_server.cpp
    )
    target_compile_definitions(sideshow-player PRIVATE ${SIDESHOW_GL_DEFS})
    target_compile_options(sideshow-player PRIVATE -Wall -Wextra)
    target_link_libraries(sideshow-player PRIVATE
        sideshow_backends SDL2::SDL2 Threads::Threads ${SIDESHOW_GL_LIBS}
        libprojectM::projectM libprojectM::playlist PkgConfig::LIBLO)
endif()

if(SIDESHOW_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    add_executable(sideshow_tests
        tests/test_isf_playback.cpp
        tests/test_isf_budget.cpp
        tests/test_isf_images.cpp
        tests/test_isf_loader.cpp
        tests/test_isf_parameters.cpp
        tests/test_isf_audio.cpp
        tests/test_isf_document.cpp
        tests/test_isf_catalog.cpp
        tests/test_video_catalog.cpp
        tests/test_video_playback.cpp
        tests/test_visual_controller.cpp
        tests/test_fluid_playback.cpp
        tests/test_fluid_presets.cpp
        tests/test_output_level.cpp
        tests/test_command_queue.cpp
        tests/test_osc_parse.cpp
        tests/test_audio_ring.cpp
        tests/test_config.cpp
        tests/test_preset_catalog.cpp
        tests/test_display_mode.cpp
    )
    target_compile_definitions(sideshow_tests PRIVATE ISF_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/isf")
    target_link_libraries(sideshow_tests PRIVATE sideshow_core Catch2::Catch2WithMain)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(sideshow_tests)

    if(SIDESHOW_BUILD_VIDEO_TESTS)
        add_executable(sideshow_video_tests tests/test_video_decoder.cpp tests/test_video_worker.cpp)
        target_compile_definitions(sideshow_video_tests PRIVATE VIDEO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/video")
        target_link_libraries(sideshow_video_tests PRIVATE sideshow_video Catch2::Catch2WithMain)
        catch_discover_tests(sideshow_video_tests)
        # Observe real decoder allocations without adding hooks to the player.
        add_executable(sideshow_video_limit_tests tests/test_video_limits.cpp src/video_decoder.cpp)
        target_compile_options(sideshow_video_limit_tests PRIVATE
            -include ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/video_decode_tracking.h)
        target_compile_definitions(sideshow_video_limit_tests PRIVATE VIDEO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/video")
        target_link_libraries(sideshow_video_limit_tests PRIVATE sideshow_video Catch2::Catch2WithMain)
        catch_discover_tests(sideshow_video_limit_tests)
    endif()

    # Compile the real manager against a test-only projectM C API double, with no GPU SDK.
    add_executable(sideshow_preset_tests
        tests/test_preset_manager.cpp
        tests/support/fake_projectm.cpp
        src/preset_manager.cpp)
    target_include_directories(sideshow_preset_tests PRIVATE tests/support)
    target_link_libraries(sideshow_preset_tests PRIVATE sideshow_core Catch2::Catch2WithMain)
    catch_discover_tests(sideshow_preset_tests)
endif()

option(SIDESHOW_BUILD_ISF_TESTS "Build real-context ISF tests" OFF)
if(SIDESHOW_BUILD_PLAYER OR SIDESHOW_BUILD_ISF_TESTS)
    include(cmake/IsfDependencies.cmake)
endif()
if(SIDESHOW_BUILD_ISF_TESTS)
    find_package(SDL2 REQUIRED)
    add_executable(sideshow_isf_context_tests tests/test_isf_context_gl.cpp)
    target_include_directories(sideshow_isf_context_tests PRIVATE src)
    target_link_libraries(sideshow_isf_context_tests PRIVATE SDL2::SDL2 sideshow_isf_context)
endif()
if(SIDESHOW_BUILD_PLAYER AND SIDESHOW_BUILD_TESTS)
    add_executable(sideshow_isf_wire_tests tests/test_isf_osc_wire.cpp src/osc_server.cpp)
    target_link_libraries(sideshow_isf_wire_tests PRIVATE sideshow_core PkgConfig::LIBLO Catch2::Catch2WithMain)
    catch_discover_tests(sideshow_isf_wire_tests)
endif()
if(SIDESHOW_BUILD_PLAYER OR SIDESHOW_BUILD_ISF_TESTS)
    add_library(sideshow_isf_runtime STATIC src/isf_runtime.cpp src/isf_presenter.cpp src/fluid_gl.cpp src/fluid_shaders.cpp)
    target_link_libraries(sideshow_isf_runtime PUBLIC sideshow_isf_context sideshow_core)
    target_sources(sideshow_isf_runtime PRIVATE src/isf_backend.cpp)
endif()
if(SIDESHOW_BUILD_ISF_TESTS)
    add_executable(sideshow_isf_runtime_tests tests/test_isf_runtime_gl.cpp)
    target_compile_definitions(sideshow_isf_runtime_tests PRIVATE ISF_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/isf")
    target_link_libraries(sideshow_isf_runtime_tests PRIVATE sideshow_isf_runtime SDL2::SDL2)
endif()
if(SIDESHOW_BUILD_ISF_TESTS)
    add_executable(sideshow_isf_backend_tests tests/test_isf_backend_gl.cpp)
    target_link_libraries(sideshow_isf_backend_tests PRIVATE sideshow_isf_runtime SDL2::SDL2)
endif()

if(SIDESHOW_BUILD_PLAYER)
    target_link_libraries(sideshow-player PRIVATE sideshow_isf_runtime)
endif()
