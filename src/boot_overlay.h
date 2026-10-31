#pragma once

#include <optional>
#include <string>
#include <vector>

namespace sideshow {

// Pure helpers for the boot address overlay (no GL): text, layout and timing.

// One-channel bitmap of text in a 5x7 pixel font, one pixel of spacing between characters
// and a `padding`-pixel empty border. Row 0 is the top. 255 = lit, 0 = empty. Lowercase
// letters, digits, space, '.', ':', '-' and '/' are drawn; anything else is drawn as '?'.
struct TextBitmap {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};
TextBitmap rasterizeText(const std::string& text, int padding = 2);

// Non-loopback IPv4 addresses of interfaces that are up, sorted, e.g. {"192.168.1.23"}.
std::vector<std::string> localIpv4Addresses();

// "panel-01  192.168.1.23" (several addresses separated by two spaces), or
// "panel-01  no network" when there are none.
std::string bootOverlayText(const std::string& panelId, const std::vector<std::string>& addresses);

// Overlay opacity `elapsed` seconds after the player started: 1 while showing, fading to 0
// over the last second, 0 afterwards. Shows only when the system had booted recently
// (uptime at start below 300 s, or unknown), so a player restart mid-show stays clean.
// `duration` <= 0 disables the overlay.
float bootOverlayAlpha(double elapsed, double duration, std::optional<double> uptimeAtStart);

// Seconds since the system booted, if known (Linux /proc/uptime).
std::optional<double> systemUptimeSeconds();

// Integer pixel scale for the 7-pixel-tall font: about 1/360 of the screen height, at least 1
// (1080 lines -> 3, 720 -> 2, 1440 -> 4, 2160 -> 6).
int bootOverlayScale(int screenHeight);

}  // namespace sideshow
