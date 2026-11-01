#pragma once

#include <optional>
#include <string>
#include <vector>

namespace sideshow {

// Pure helpers for the boot address overlay (no GL): text, layout and timing.

// One-channel bitmap of text in a 5x7 pixel font, one pixel of spacing between characters
// and a `padding`-pixel empty border. Row 0 is the top. 255 = lit, 0 = empty. Letters (upper
// case drawn as lower case), digits, space and . : - / @ _ ' ( ) are drawn; anything else is
// drawn as '?'.
struct TextBitmap {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};
TextBitmap rasterizeText(const std::string& text, int padding = 2);

// Non-loopback IPv4 addresses of interfaces that are up, sorted, e.g. {"192.168.1.23"}.
std::vector<std::string> localIpv4Addresses();

struct BootOverlayInfo {
    std::string panelId;
    std::vector<std::string> addresses;  // IPv4, first one is used for the ssh hint
    std::string loginUser;               // "" if unknown
    // Wi-Fi network name; "" = not connected; nullopt = unknown (not shown).
    std::optional<std::string> wifi;
};
// "panel-01  ssh pi@172.20.10.3  wifi: Josh's iPhone"; further addresses follow as
// "also 169.254.4.2"; "panel-01  no network  wifi: none" without an address.
std::string bootOverlayText(const BootOverlayInfo& info);

// Name of the first regular account (UID 1000, the one Raspberry Pi Imager creates), or "".
std::string loginUserName();

// SSID of the active Wi-Fi network from `nmcli -t -f ACTIVE,SSID dev wifi list` output
// ("yes:Name" lines, ':' escaped as "\:"); "" when none is active.
std::string parseActiveWifi(const std::string& nmcliOutput);

// Runs nmcli (at most ~3 s; call off the render thread). nullopt if nmcli is unavailable.
std::optional<std::string> currentWifiName();

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
