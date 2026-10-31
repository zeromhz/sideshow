#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <exception>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "audio_capture.h"
#include "audio_feed.h"
#include "audio_ring.h"
#include "command.h"
#include "config.h"
#include "display.h"
#include "log.h"
#include "display_mode.h"
#include "gl.h"
#include "osc_server.h"
#include "output_level.h"
#include "preset_catalog.h"
#include "preset_manager.h"
#include "renderer.h"
#include "boot_overlay.h"
#include "boot_overlay_gl.h"
#include "visual_controller.h"
#include "milkdrop_backend.h"
#include "fluid_backend.h"
#include "video_backend.h"
#include "isf_backend.h"
#include "isf_services.h"
#include "brightness_pass.h"

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

// --warm-cache [playlist]: nullopt when absent; the playlist defaults to the configured one.
std::optional<std::string> warmPlaylistFromArgs(int argc, char** argv, const std::string& fallback) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--warm-cache") != 0) continue;
        if (i + 1 < argc && argv[i + 1][0] != '-') return std::string(argv[i + 1]);
        return fallback;
    }
    return std::nullopt;
}

// Loads every preset of a playlist once and renders it at the panel's real size, so Mesa's
// on-disk shader cache (~/.cache/mesa_shader_cache of the user running it) holds every compiled
// shader before a show. Later preset switches then skip shader compilation, which is most of
// their hitch. Run it as the service user with the service stopped; re-run after Mesa upgrades.
int warmShaderCache(const Config& config, Display& display, const std::string& playlist) {
    PresetCatalog catalog(config.presets.root);
    const std::vector<std::string> files = catalog.presetsIn(playlist);
    if (files.empty()) {
        logError("warm-cache: playlist '" + playlist + "' is missing or empty");
        return 1;
    }
    Renderer renderer(display.drawableWidth(), display.drawableHeight(), config.display.fps,
                      config.presets);
    logInfo("warm-cache: " + std::to_string(files.size()) + " presets from '" + playlist + "'");
    std::vector<std::pair<double, std::string>> slowest;
    const auto started = std::chrono::steady_clock::now();
    for (size_t i = 0; i < files.size() && !g_signalled.load(); ++i) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) g_signalled.store(true);
        }
        const auto before = std::chrono::steady_clock::now();
        projectm_load_preset_file(renderer.handle(), files[i].c_str(), false);
        // The driver finishes compiling on first use, so draw twice before timing stops.
        for (int frame = 0; frame < 2; ++frame) renderer.renderFrame();
        glFinish();
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - before).count();
        display.swap();
        slowest.emplace_back(ms, files[i]);
        if ((i + 1) % 100 == 0 || i + 1 == files.size()) {
            logInfo("warm-cache: " + std::to_string(i + 1) + "/" + std::to_string(files.size()));
        }
    }
    std::sort(slowest.rbegin(), slowest.rend());
    for (size_t i = 0; i < std::min<size_t>(10, slowest.size()); ++i) {
        logInfo("warm-cache: slowest " + std::to_string(static_cast<int>(slowest[i].first)) +
                " ms  " + slowest[i].second);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    logInfo("warm-cache: done in " + std::to_string(static_cast<int>(seconds)) + " s");
    return g_signalled.load() ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    const std::string configPath = configPathFromArgs(argc, argv);
    ConfigResult loaded = loadConfigFile(configPath);
    for (const auto& warning : loaded.warnings) logWarn(warning);
    const Config& config = loaded.config;
    logInfo("sideshow-player starting as " + config.panelId + " (config: " + configPath + ")");

    // KMS/DRM: make each swap wait for its page flip, so a swap returns at the vblank where the
    // frame appears. The frame pacing below uses that to hold every frame for whole refreshes.
    // Builds that ignore the hint keep the plain frame limiter.
    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        logError(std::string("SDL_Init failed (no connected display? systemd will retry): ") +
                 SDL_GetError());
        return 1;
    }
    const bool audioAvailable = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
    if (!audioAvailable) {
        logWarn(std::string("audio unavailable: ") + SDL_GetError() + "; rendering without audio");
    }
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    int exitCode = 0;
    try {
        // Declaration order matters: later objects are destroyed first.
        if (auto playlist = warmPlaylistFromArgs(argc, argv, config.presets.playlist)) {
            {
                Display display(config.display);
                exitCode = warmShaderCache(config, display, *playlist);
            }
            SDL_Quit();
            return exitCode;
        }
        Display display(config.display);
        IsfServices isfServices(config.isf);
        VideoWorker videoWorker(config.video.root);
        VisualController visuals(config,display.drawableWidth(),display.drawableHeight(),
            [&videoWorker,&isfServices](VisualMode mode,const BackendInit& init)->std::unique_ptr<VisualBackend> {
                if(mode==VisualMode::Isf) return std::make_unique<IsfBackend>(init,isfServices.loader(),isfServices.context(),isfServices.budget());
                if(mode==VisualMode::Video) return std::make_unique<VideoBackend>(init,videoWorker);
                if(mode==VisualMode::Fluid)return std::make_unique<FluidBackend>(init);
                return std::make_unique<MilkdropBackend>(init);
            });
        BrightnessPass brightness;
        // Panel ID and IP address in the bottom-right corner for the first minute after boot.
        BootOverlay bootOverlay;
        const auto uptimeAtStart = systemUptimeSeconds();
        const Uint64 startCounter = SDL_GetPerformanceCounter();
        double lastAddressCheck = -1.0;
        logInfo(bootOverlayText(config.panelId, localIpv4Addresses()));

        OutputLevel output(config.output.masterDefault, config.output.masterCurve,
                           config.output.masterSmoothingMs);
        AudioRing ring(static_cast<size_t>(config.audio.sampleRate / 4) * 2);  // ~250 ms stereo
        std::optional<AudioCapture> audio;  // empty when SDL audio is unavailable
        if (audioAvailable) audio.emplace(config.audio.device, config.audio.sampleRate, ring);
        CommandQueue commands;
        PlayerStatus status;
        OscServer osc(config.osc.port, config.panelId, commands, status);

        AudioFeed audioFeed(projectm_pcm_get_max_samples());
        // Frame-rate ceiling on an absolute schedule: frames start at most every 1/fps seconds,
        // so the average never exceeds display.fps even when vsync runs faster. A late frame
        // does not cause a burst to catch up.
        const Uint64 counterRate = SDL_GetPerformanceFrequency();
        const Uint64 framePeriod = counterRate / static_cast<Uint64>(std::max(1, config.display.fps));
        // Vblank alignment: with holdRefreshes = 2 (60 Hz at 30 fps), a frame is swapped
        // only after the vblank that follows the previous frame's flip, so it flips on the one
        // after: exactly two refreshes per frame, with the whole time available for rendering.
        const int refresh = display.refreshRate();
        const int holdRefreshes = refreshesPerFrame(refresh, config.display.fps);
        const Uint64 refreshPeriod = refresh > 0 ? counterRate / static_cast<Uint64>(refresh) : 0;
        const Uint64 flipMargin = counterRate * 3 / 2000;  // 1.5 ms past the in-between vblank
        Uint64 lastFlip = 0;
        if (holdRefreshes > 1) {
            logInfo("frame pacing: each frame held for " + std::to_string(holdRefreshes) +
                    " refreshes at " + std::to_string(refresh) + " Hz");
        }
        auto waitUntil = [counterRate](Uint64 deadline) {
            Uint64 counter = SDL_GetPerformanceCounter();
            if (counter >= deadline) return counter;
            // Sleep to within 0.3 ms of the deadline, then yield the rest.
            const Uint64 slack = counterRate * 3 / 10000;
            if (deadline - counter > slack) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(
                    (deadline - counter - slack) * 1000000000ull / counterRate));
            }
            while ((counter = SDL_GetPerformanceCounter()) < deadline) std::this_thread::yield();
            return counter;
        };
        Uint64 nextFrame = SDL_GetPerformanceCounter() + framePeriod;
        Uint32 lastTicks = SDL_GetTicks();
        Uint32 fpsWindowStart = lastTicks;
        int framesInWindow = 0;
        bool running = true;

        while (running) {
            if (g_signalled.load()) running = false;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (event.type == SDL_AUDIODEVICEREMOVED && event.adevice.iscapture) {
                    if (audio) audio->handleDeviceRemoved(event.adevice.which);
                } else if (event.type == SDL_WINDOWEVENT &&
                           event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    visuals.resize(display.drawableWidth(), display.drawableHeight());
                }
            }

            // Skip a failed preset first, so it can't override or double an OSC command.
            visuals.maintenance();
            commands.processAcknowledgedFrame([&](const Command& command) {
                if(const auto* c=std::get_if<cmd::Master>(&command)) {
                    output.setTarget(c->value);
                    return CommandResult{"applied","master="+std::to_string(c->value)};
                }
                if(const auto* c=std::get_if<cmd::Blackout>(&command)) {
                    output.setBlackout(c->on);
                    return CommandResult{"applied",c->on ? "blackout=1" : "blackout=0"};
                }
                auto result=visuals.apply(command);
                if(result.status=="rejected") logWarn("OSC: "+result.detail);
                return result;
            });

            const Uint32 now = SDL_GetTicks();
            if (audio) audio->tick(now);
            const float dtSeconds = static_cast<float>(now - lastTicks) / 1000.0f;
            bool audioFresh=audioFeed.update(ring, audio && audio->isOpen(), dtSeconds,
                             [&](const float* pcm, size_t frames) {
                                 visuals.addPcm(pcm, frames);
                             });

            if(!audioFresh) visuals.clearAudioHistory();
            output.update(dtSeconds);
            lastTicks = now;
            visuals.render(dtSeconds);
            const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - startCounter) /
                                   static_cast<double>(SDL_GetPerformanceFrequency());
            const float overlayAlpha =
                bootOverlayAlpha(elapsed, config.display.ipOverlaySeconds, uptimeAtStart);
            if (overlayAlpha > 0.0f) {
                // Re-check every second: Wi-Fi often joins after the player has started.
                if (lastAddressCheck < 0.0 || elapsed - lastAddressCheck >= 1.0) {
                    bootOverlay.setText(bootOverlayText(config.panelId, localIpv4Addresses()));
                    lastAddressCheck = elapsed;
                }
                bootOverlay.draw(overlayAlpha, display.drawableWidth(), display.drawableHeight());
            }
            brightness.apply(output.brightness(),display.drawableWidth(),display.drawableHeight());
            if (holdRefreshes > 1 && lastFlip != 0) {
                waitUntil(lastFlip + static_cast<Uint64>(holdRefreshes - 1) * refreshPeriod +
                          flipMargin);
            }
            display.swap();
            lastFlip = SDL_GetPerformanceCounter();

            ++framesInWindow;
            if (now - fpsWindowStart >= 1000) {
                status.set(visuals.statusPreset(),
                           framesInWindow * 1000.0f / static_cast<float>(now - fpsWindowStart));
                fpsWindowStart = now;
                framesInWindow = 0;
            }

            const Uint64 counter = waitUntil(nextFrame);
            nextFrame += framePeriod;
            if (nextFrame < counter) nextFrame = counter;
        }
    } catch (const std::exception& e) {
        logError(e.what());
        exitCode = 1;
    }
    SDL_Quit();
    return exitCode;
}
