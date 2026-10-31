#include <catch2/catch_test_macros.hpp>

#include "boot_overlay.h"

#include <algorithm>
#include <string>

using namespace sideshow;

namespace {
bool lit(const TextBitmap& b, int x, int y) { return b.pixels[static_cast<size_t>(y) * b.width + x] != 0; }
}  // namespace

TEST_CASE("Boot overlay text names the panel and its addresses") {
    CHECK(bootOverlayText("panel-01", {"192.168.1.23"}) == "panel-01  192.168.1.23");
    CHECK(bootOverlayText("panel-02", {"10.0.0.5", "192.168.1.9"}) == "panel-02  10.0.0.5  192.168.1.9");
    CHECK(bootOverlayText("dev", {}) == "dev  no network");
}

TEST_CASE("Text rasterizes to 5x7 glyphs with spacing and padding") {
    TextBitmap b = rasterizeText("1.", 2);
    CHECK(b.width == 2 * 6 - 1 + 4);
    CHECK(b.height == 7 + 4);
    REQUIRE(b.pixels.size() == static_cast<size_t>(b.width * b.height));
    // Padding stays empty.
    for (int x = 0; x < b.width; ++x) CHECK_FALSE(lit(b, x, 0));
    // '1': the stem runs down the middle column, with a three-wide foot on the bottom row.
    for (int y = 2; y < 9; ++y) CHECK(lit(b, 2 + 2, y));
    CHECK(lit(b, 2 + 1, 8));
    CHECK(lit(b, 2 + 3, 8));
    // '.' sits on the bottom two rows of the second cell.
    CHECK(lit(b, 2 + 6 + 1, 8));
    CHECK_FALSE(lit(b, 2 + 6 + 1, 2));
    // The spacing column between glyphs is empty.
    for (int y = 0; y < b.height; ++y) CHECK_FALSE(lit(b, 2 + 5, y));
}

TEST_CASE("Every supported character has a glyph distinct from the fallback") {
    const TextBitmap fallback = rasterizeText("?", 0);
    for (char c : std::string("0123456789abcdefghijklmnopqrstuvwxyz.:-/")) {
        const TextBitmap b = rasterizeText(std::string(1, c), 0);
        CHECK(b.pixels != fallback.pixels);
        CHECK(b.pixels != rasterizeText(" ", 0).pixels);
    }
    CHECK(rasterizeText("A", 0).pixels == rasterizeText("a", 0).pixels);
    CHECK(rasterizeText("~", 0).pixels == fallback.pixels);
    CHECK(rasterizeText("", 1).width == 2);
}

TEST_CASE("Boot overlay shows for its duration after a fresh boot, then fades") {
    CHECK(bootOverlayAlpha(0.0, 60.0, 20.0) == 1.0f);
    CHECK(bootOverlayAlpha(30.0, 60.0, 20.0) == 1.0f);
    CHECK(bootOverlayAlpha(59.5, 60.0, 20.0) == 0.5f);
    CHECK(bootOverlayAlpha(60.0, 60.0, 20.0) == 0.0f);
    CHECK(bootOverlayAlpha(10.0, 60.0, std::nullopt) == 1.0f);  // uptime unknown (macOS dev)
    CHECK(bootOverlayAlpha(10.0, 60.0, 3600.0) == 0.0f);        // player restarted mid-show
    CHECK(bootOverlayAlpha(10.0, 0.0, 20.0) == 0.0f);           // disabled
}

TEST_CASE("Boot overlay type scales with the screen height") {
    CHECK(bootOverlayScale(720) == 2);
    CHECK(bootOverlayScale(1080) == 3);
    CHECK(bootOverlayScale(1440) == 4);
    CHECK(bootOverlayScale(2160) == 6);
    CHECK(bootOverlayScale(100) == 1);
}

TEST_CASE("Local addresses are well-formed IPv4 and exclude loopback") {
    for (const auto& address : localIpv4Addresses()) {
        CHECK(address.rfind("127.", 0) != 0);
        CHECK(std::count(address.begin(), address.end(), '.') == 3);
    }
}
