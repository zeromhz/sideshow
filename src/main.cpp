#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <exception>
#include <string>

#include "config.h"
#include "display.h"
#include "gl.h"
#include "log.h"

using namespace sideshow;

namespace {

constexpr const char* kDefaultConfigPath = "/etc/sideshow/config.toml";

// Some SDL builds (e.g. sdl2-compat on top of SDL3) do not reliably convert a delivered
// SIGINT/SIGTERM into an SDL_QUIT event, so we also track it ourselves as a belt-and-braces
// shutdown path for `systemctl stop` / Ctrl+C.
std::atomic<bool> g_signalled{false};

void handleSignal(int /*signum*/) { g_signalled.store(true); }

std::string configPathFromArgs(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) return argv[i + 1];
    }
    return kDefaultConfigPath;
}

}  // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    const std::string configPath = configPathFromArgs(argc, argv);
    ConfigResult loaded = loadConfigFile(configPath);
    for (const auto& warning : loaded.warnings) logWarn(warning);
    const Config& config = loaded.config;
    logInfo("sideshow-player starting as " + config.panelId + " (config: " + configPath + ")");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        logError(std::string("SDL_Init failed (no connected display? systemd will retry): ") +
                 SDL_GetError());
        return 1;
    }
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    int exitCode = 0;
    try {
        Display display(config.display);
        bool running = true;
        while (running) {
            if (g_signalled.load()) running = false;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
            }
            glViewport(0, 0, display.drawableWidth(), display.drawableHeight());
            glClearColor(0.0f, 0.0f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            display.swap();
        }
    } catch (const std::exception& e) {
        logError(e.what());
        exitCode = 1;
    }
    SDL_Quit();
    return exitCode;
}
