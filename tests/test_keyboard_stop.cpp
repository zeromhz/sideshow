#include <catch2/catch_test_macros.hpp>

#include "keyboard_stop.h"

using namespace sideshow;

TEST_CASE("Only Ctrl+Alt+Q, pressed rather than repeated, stops the player") {
    CHECK(isKeyboardStop('q', true, true, false));
    CHECK_FALSE(isKeyboardStop('q', true, true, true));    // key repeat
    CHECK_FALSE(isKeyboardStop('q', true, false, false));  // Ctrl+Q
    CHECK_FALSE(isKeyboardStop('q', false, true, false));  // Alt+Q
    CHECK_FALSE(isKeyboardStop('q', false, false, false));
    CHECK_FALSE(isKeyboardStop('w', true, true, false));
    CHECK(kKeyboardStopExitCode == 80);  // RestartPreventExitStatus in sideshow-player.service
}
