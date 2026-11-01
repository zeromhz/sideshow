#include "boot_overlay.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>

#if defined(__linux__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <pwd.h>
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
    static const Glyph at = {" ### ", "#   #", "# ###", "# # #", "# ###", "#    ", " ####"};
    static const Glyph underscore = {"     ", "     ", "     ", "     ", "     ", "     ", "#####"};
    static const Glyph apostrophe = {"  #  ", "  #  ", " #   ", "     ", "     ", "     ", "     "};
    static const Glyph open = {"   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # "};
    static const Glyph close = {" #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   "};

    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isdigit(u)) return &digits[c - '0'];
    if (std::isalpha(u)) return &letters[std::tolower(u) - 'a'];
    switch (c) {
        case ' ': return &blank;
        case '.': return &dot;
        case ':': return &colon;
        case '-': return &dash;
        case '/': return &slash;
        case '@': return &at;
        case '_': return &underscore;
        case '\'': return &apostrophe;
        case '(': return &open;
        case ')': return &close;
        default: return &unknown;
    }
}

}  // namespace

namespace {
// One drawable character per UTF-8 sequence: curly quotes (common in iPhone hotspot names) become
// an apostrophe, any other non-ASCII character a single '?'.
std::string drawableCharacters(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) { out += text[i++]; continue; }
        size_t length = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        length = std::min(length, text.size() - i);
        const std::string sequence = text.substr(i, length);
        out += sequence == "\xE2\x80\x98" || sequence == "\xE2\x80\x99" ? '\'' : '?';
        i += length;
    }
    return out;
}
}  // namespace

TextBitmap rasterizeText(const std::string& source, int padding) {
    const std::string text = drawableCharacters(source);
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

std::string bootOverlayText(const BootOverlayInfo& info) {
    std::string text = info.panelId + "  ";
    if (info.addresses.empty()) {
        text += "no network";
    } else {
        text += "ssh " + (info.loginUser.empty() ? "" : info.loginUser + "@") + info.addresses[0];
        for (size_t i = 1; i < info.addresses.size(); ++i) text += "  also " + info.addresses[i];
    }
    if (info.wifi) text += "  wifi: " + (info.wifi->empty() ? std::string("none") : *info.wifi);
    return text;
}

std::string loginUserName() {
#if defined(__linux__) || defined(__APPLE__)
    if (const passwd* entry = getpwuid(1000); entry && entry->pw_name) return entry->pw_name;
#endif
    return {};
}

std::string parseActiveWifi(const std::string& output) {
    size_t start = 0;
    while (start < output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        const std::string line = output.substr(start, end - start);
        start = end + 1;
        if (line.rfind("yes:", 0) != 0) continue;
        std::string ssid;
        for (size_t i = 4; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size()) ++i;  // nmcli escapes ':' and '\'
            ssid += line[i];
        }
        return ssid;
    }
    return {};
}

std::optional<std::string> currentWifiName() {
#if defined(__linux__)
    FILE* pipe = popen("timeout 3 nmcli -t -f ACTIVE,SSID dev wifi list --rescan no 2>/dev/null", "r");
    if (!pipe) return std::nullopt;
    std::string output;
    char buffer[256];
    while (size_t n = fread(buffer, 1, sizeof(buffer), pipe)) output.append(buffer, n);
    const int status = pclose(pipe);
    if (status != 0 && output.empty()) return std::nullopt;  // nmcli missing or failed
    return parseActiveWifi(output);
#else
    return std::nullopt;
#endif
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
