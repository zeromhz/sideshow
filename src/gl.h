#pragma once

// SIDESHOW_GLES is defined by CMake on Linux (Raspberry Pi, OpenGL ES 3).
#if defined(SIDESHOW_GLES)
#include <GLES3/gl3.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#error "Unsupported platform: build on macOS or with SIDESHOW_GLES (Linux/Raspberry Pi)"
#endif
