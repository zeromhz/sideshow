#pragma once

#include <SDL.h>

#include "config.h"

namespace sideshow {

// SDL window + OpenGL context. On the Pi (no X/Wayland) SDL uses KMS/DRM directly.
class Display {
public:
    explicit Display(const Config::Display& config);  // throws std::runtime_error
    ~Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    void swap();
    int drawableWidth() const;
    int drawableHeight() const;

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
};

}  // namespace sideshow
