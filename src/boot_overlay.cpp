#include "boot_overlay.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

#if defined(__linux__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace sideshow {
namespace {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
using Glyph = std::array<const char*, kGlyphHeight>;

// '#' = lit. Lowercase letters sit on the same baseline as digits (no descenders).
const Glyph* glyphFor(char c) {
    static const Glyph blank = {"     ", "     ", "     ", "     ", "     ", "     ", "     "};
    static const Glyph unknown = {" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "};
    static const Glyph digits[10] = {
        {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "},
        {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "},
        {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"},
        {"#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "},
        {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "},
        {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "},
        {"  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "},
        {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "},
        {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "},
        {" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "},
    };
    static const Glyph letters[26] = {
        {"     ", "     ", " ### ", "    #", " ####", "#   #", " ####"},  // a
        {"#    ", "#    ", "# ## ", "##  #", "#   #", "#   #", "#### "},  // b
        {"     ", "     ", " ### ", "#    ", "#    ", "#   #", " ### "},  // c
        {"    #", "    #", " ## #", "#  ##", "#   #", "#   #", " ####"},  // d
        {"     ", "     ", " ### ", "#   #", "#####", "#    ", " ### "},  // e
        {"  ## ", " #  #", " #   ", "###  ", " #   ", " #   ", " #   "},  // f
        {"     ", " ####", "#   #", "#   #", " ####", "    #", " ### "},  // g
        {"#    ", "#    ", "# ## ", "##  #", "#   #", "#   #", "#   #"},  // h
        {"  #  ", "     ", " ##  ", "  #  ", "  #  ", "  #  ", " ### "},  // i
        {"   # ", "     ", "  ## ", "   # ", "   # ", "#  # ", " ##  "},  // j
        {"#    ", "#    ", "#  # ", "# #  ", "##   ", "# #  ", "#  # "},  // k
        {" ##  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "},  // l
        {"     ", "     ", "## # ", "# # #", "# # #", "#   #", "#   #"},  // m
        {"     ", "     ", "# ## ", "##  #", "#   #", "#   #", "#   #"},  // n
        {"     ", "     ", " ### ", "#   #", "#   #", "#   #", " ### "},  // o
        {"     ", "     ", "#### ", "#   #", "#### ", "#    ", "#    "},  // p
        {"     ", "     ", " ## #", "#  ##", " ####", "    #", "    #"},  // q
        {"     ", "     ", "# ## ", "##  #", "#    ", "#    ", "#    "},  // r
        {"     ", "     ", " ### ", "#    ", " ### ", "    #", "#### "},  // s
        {" #   ", " #   ", "###  ", " #   ", " #   ", " #  #", "  ## "},  // t
        {"     ", "     ", "#   #", "#   #", "#   #", "#  ##", " ## #"},  // u
        {"     ", "     ", "#   #", "#   #", "#   #", " # # ", "  #  "},  // v
        {"     ", "     ", "#   #", "#   #", "# # #", "# # #", " # # "},  // w
        {"     ", "     ", "#   #", " # # ", "  #  ", " # # ", "#   #"},  // x
        {"     ", "     ", "#   #", "#   #", " ####", "    #", " ### "},  // y
        {"     ", "     ", "#####", "   # ", "  #  ", " #   ", "#####"},  // z
    };
    static const Glyph dot = {"     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "};
    static const Glyph colon = {"     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "};
    static const Glyph dash = {"     ", "     ", "     ", "#####", "     ", "     ", "     "};
    static const Glyph slash = {"     ", "    #", "   # ", "  #  ", " #   ", "#    ", "     "};

    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isdigit(u)) return &digits[c - '0'];
    if (std::isalpha(u)) return &letters[std::tolower(u) - 'a'];
    switch (c) {
        case ' ': return &blank;
        case '.': return &dot;
        case ':': return &colon;
        case '-': return &dash;
        case '/': return &slash;
        default: return &unknown;
    }
}

}  // namespace

TextBitmap rasterizeText(const std::string& text, int padding) {
    padding = std::max(0, padding);
    TextBitmap bitmap;
    const int count = static_cast<int>(text.size());
    const int textWidth = count > 0 ? count * (kGlyphWidth + 1) - 1 : 0;
    bitmap.width = textWidth + 2 * padding;
    bitmap.height = kGlyphHeight + 2 * padding;
    bitmap.pixels.assign(static_cast<size_t>(bitmap.width) * bitmap.height, 0);
    for (int i = 0; i < count; ++i) {
        const Glyph& glyph = *glyphFor(text[i]);
        const int left = padding + i * (kGlyphWidth + 1);
        for (int row = 0; row < kGlyphHeight; ++row) {
            for (int column = 0; column < kGlyphWidth; ++column) {
                if (glyph[row][column] != '#') continue;
                bitmap.pixels[static_cast<size_t>(padding + row) * bitmap.width + left + column] = 255;
            }
        }
    }
    return bitmap;
}

std::vector<std::string> localIpv4Addresses() {
    std::vector<std::string> result;
#if defined(__linux__) || defined(__APPLE__)
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return result;
    for (const ifaddrs* entry = list; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET) continue;
        if (!(entry->ifa_flags & IFF_UP) || (entry->ifa_flags & IFF_LOOPBACK)) continue;
        char text[INET_ADDRSTRLEN] = {};
        const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) result.emplace_back(text);
    }
    freeifaddrs(list);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
#endif
    return result;
}

std::string bootOverlayText(const std::string& panelId, const std::vector<std::string>& addresses) {
    std::string text = panelId + "  ";
    if (addresses.empty()) return text + "no network";
    for (size_t i = 0; i < addresses.size(); ++i) {
        if (i > 0) text += "  ";
        text += addresses[i];
    }
    return text;
}

float bootOverlayAlpha(double elapsed, double duration, std::optional<double> uptimeAtStart) {
    if (duration <= 0.0 || elapsed < 0.0 || elapsed >= duration) return 0.0f;
    if (uptimeAtStart && *uptimeAtStart >= 300.0) return 0.0f;
    return static_cast<float>(std::min(1.0, duration - elapsed));
}

std::optional<double> systemUptimeSeconds() {
    std::ifstream file("/proc/uptime");
    double seconds = 0.0;
    if (file >> seconds) return seconds;
    return std::nullopt;
}

int bootOverlayScale(int screenHeight) { return std::max(1, (screenHeight + 180) / 360); }

}  // namespace sideshow
