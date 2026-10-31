#pragma once

namespace sideshow {

// Ctrl+Alt+Q on a keyboard plugged into the Pi stops the player and opens a login prompt on
// the panel's screen (see setup/sideshow-player.service), for when the panel is on a network
// you cannot reach. Console switching (Ctrl+Alt+F2) does not work while the player owns the
// screen and keyboard.
constexpr int kKeyboardStopExitCode = 80;  // matches RestartPreventExitStatus in the service

// keycode: SDL keycode (SDLK_q == 'q'); ctrl/alt: either side held.
constexpr bool isKeyboardStop(int keycode, bool ctrl, bool alt, bool repeat) {
    return keycode == 'q' && ctrl && alt && !repeat;
}

}  // namespace sideshow
