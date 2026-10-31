#include "display.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "display_mode.h"
#include "gl.h"
#include "log.h"

namespace sideshow {
namespace {

std::string describe(const DisplayMode& m) {
    return std::to_string(m.width) + "x" + std::to_string(m.height) + "@" +
           std::to_string(m.refreshRate) + "Hz";
}

// Reads the screen's modes (from EDID) and picks one per config: explicit size if the screen
// supports it, else the native size capped at max_width x max_height.
SDL_DisplayMode chooseFullscreenMode(const Config::Display& config) {
    if (SDL_GetNumVideoDisplays() < 1) {
        throw std::runtime_error("no connected display (systemd will retry)");
    }
    SDL_DisplayMode native{};
    if (SDL_GetDesktopDisplayMode(0, &native) != 0) {
        throw std::runtime_error(std::string("cannot read display mode: ") + SDL_GetError());
    }
    const DisplayMode nativeInfo{native.w, native.h, native.refresh_rate};

    std::vector<SDL_DisplayMode> sdlModes;
    std::vector<DisplayMode> modes;
    const int count = SDL_GetNumDisplayModes(0);
    for (int i = 0; i < count; ++i) {
        SDL_DisplayMode m{};
        if (SDL_GetDisplayMode(0, i, &m) == 0) {
            sdlModes.push_back(m);
            modes.push_back({m.w, m.h, m.refresh_rate});
        }
    }

    std::optional<size_t> pick;
    if (config.width > 0 && config.height > 0) {
        pick = findExactMode(modes, config.width, config.height);
        if (!pick) {
            logWarn("display mode " + std::to_string(config.width) + "x" +
                    std::to_string(config.height) +
                    " not supported by this screen; choosing automatically");
        }
    }
    if (!pick) pick = pickAutoMode(modes, nativeInfo, config.maxWidth, config.maxHeight);
    if (!pick) {
        logWarn("no display mode fits within " + std::to_string(config.maxWidth) + "x" +
                std::to_string(config.maxHeight) + "; using native " + describe(nativeInfo));
        return native;
    }
    logInfo("screen native " + describe(nativeInfo) + ", using " + describe(modes[*pick]));
    return sdlModes[*pick];
}

}  // namespace

Display::Display(const Config::Display& config) {
#ifdef SIDESHOW_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
    int width = config.width > 0 ? config.width : 1280;  // windowed size (dev)
    int height = config.height > 0 ? config.height : 720;
    SDL_DisplayMode mode{};
    if (config.fullscreen) {
        mode = chooseFullscreenMode(config);
        width = mode.w;
        height = mode.h;
        flags |= SDL_WINDOW_FULLSCREEN;
    } else {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    window_ = SDL_CreateWindow("sideshow", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               width, height, flags);
    if (!window_) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    if (config.fullscreen && SDL_SetWindowDisplayMode(window_, &mode) != 0) {
        logWarn(std::string("SDL_SetWindowDisplayMode failed: ") + SDL_GetError());
    }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        const std::string error = SDL_GetError();
        SDL_DestroyWindow(window_);
        throw std::runtime_error("SDL_GL_CreateContext failed: " + error);
    }
    // Swap interval 1 only: SDL's KMS/DRM backend rejects anything else. The main loop holds
    // each frame for whole refreshes itself (see refreshRate()).
    refreshRate_ = mode.refresh_rate;
    if (!config.fullscreen) {
        SDL_DisplayMode current{};
        const int index = SDL_GetWindowDisplayIndex(window_);
        refreshRate_ = index >= 0 && SDL_GetCurrentDisplayMode(index, &current) == 0 ? current.refresh_rate : 0;
    }
    if (SDL_GL_SetSwapInterval(1) != 0) {
        logWarn(std::string("vsync unavailable: ") + SDL_GetError());
    }
    SDL_ShowCursor(SDL_DISABLE);

    logInfo("display " + std::to_string(drawableWidth()) + "x" + std::to_string(drawableHeight()) +
            ", " + reinterpret_cast<const char*>(glGetString(GL_VERSION)));
}

Display::~Display() {
    if (context_) SDL_GL_DeleteContext(context_);
    if (window_) SDL_DestroyWindow(window_);
}

void Display::swap() { SDL_GL_SwapWindow(window_); }

int Display::drawableWidth() const {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    return w;
}

int Display::drawableHeight() const {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    return h;
}

}  // namespace sideshow
