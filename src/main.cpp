#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "audio_capture.h"
#include "audio_ring.h"
#include "config.h"
#include "display.h"
#include "log.h"
#include "output_level.h"
#include "preset_catalog.h"
#include "preset_manager.h"
#include "renderer.h"

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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        logError(std::string("SDL_Init failed (no connected display? systemd will retry): ") +
                 SDL_GetError());
        return 1;
    }
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    int exitCode = 0;
    try {
        Display display(config.display);
        Renderer renderer(display.drawableWidth(), display.drawableHeight(), config.display.fps,
                          config.presets);
        PresetCatalog catalog(config.presets.root);
        PresetManager presets(renderer.handle(), catalog, config.presets.shuffle);
        presets.start(config.presets.playlist);
        OutputLevel output(config.output.masterDefault, config.output.masterCurve,
                           config.output.masterSmoothingMs);
        AudioRing ring(static_cast<size_t>(config.audio.sampleRate / 4) * 2);  // ~250 ms stereo
        AudioCapture audio(config.audio.device, config.audio.sampleRate, ring);

        std::vector<float> pcm(projectm_pcm_get_max_samples() * 2);
        const Uint32 frameBudgetMs = 1000 / static_cast<Uint32>(std::max(1, config.display.fps));
        Uint32 lastTicks = SDL_GetTicks();
        bool running = true;

        while (running) {
            if (g_signalled.load()) running = false;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (event.type == SDL_AUDIODEVICEREMOVED && event.adevice.iscapture) {
                    audio.handleDeviceRemoved(event.adevice.which);
                } else if (event.type == SDL_WINDOWEVENT &&
                           event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    renderer.resize(display.drawableWidth(), display.drawableHeight());
                }
            }

            const Uint32 now = SDL_GetTicks();
            audio.tick(now);
            size_t n = 0;
            while ((n = ring.read(pcm.data(), pcm.size())) > 0) {
                renderer.addPcm(pcm.data(), static_cast<unsigned int>(n / 2));
            }

            output.update(static_cast<float>(now - lastTicks) / 1000.0f);
            lastTicks = now;
            renderer.render(output.brightness());
            display.swap();

            const Uint32 frameTime = SDL_GetTicks() - now;
            if (frameTime < frameBudgetMs) SDL_Delay(frameBudgetMs - frameTime);
        }
    } catch (const std::exception& e) {
        logError(e.what());
        exitCode = 1;
    }
    SDL_Quit();
    return exitCode;
}
