#!/usr/bin/env bash
# Provision a Raspberry Pi 5 (Raspberry Pi OS Lite, Bookworm 64-bit) as a sideshow panel.
# Safe to re-run. Usage: sudo setup/install.sh [--panel-id panel-03]
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_ROOT=/opt/sideshow
CONFIG=/etc/sideshow/config.toml
CMDLINE=/boot/firmware/cmdline.txt
BOOTCONFIG=/boot/firmware/config.txt
PANEL_ID=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --panel-id) PANEL_ID="${2:?--panel-id needs a value}"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    echo "run with sudo" >&2
    exit 1
fi
if [[ -n "$PANEL_ID" && ! "$PANEL_ID" =~ ^[a-z0-9][a-z0-9-]{0,31}$ ]]; then
    echo "panel id must be lowercase letters, digits, and dashes (e.g. panel-03)" >&2
    exit 2
fi

echo "==> Installing packages"
apt-get update
apt-get install -y build-essential cmake git pkg-config \
    libsdl2-dev liblo-dev libgles-dev libegl-dev

echo "==> Building libprojectM"
"$REPO_DIR/setup/build_projectm.sh" /usr/local --gles
ldconfig

echo "==> Building sideshow-player"
BUILD_DIR="$INSTALL_ROOT/build"
cmake -S "$REPO_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DSIDESHOW_BUILD_PLAYER=ON -DSIDESHOW_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --parallel "$(nproc)"
install -D -m 0755 "$BUILD_DIR/sideshow-player" "$INSTALL_ROOT/bin/sideshow-player"

echo "==> Fetching presets and textures"
"$REPO_DIR/setup/fetch_presets.sh" "$INSTALL_ROOT"

echo "==> Writing config"
install -d /etc/sideshow
if [[ ! -f "$CONFIG" ]]; then
    install -m 0644 "$REPO_DIR/setup/config.example.toml" "$CONFIG"
fi
if [[ -n "$PANEL_ID" ]]; then
    sed -i "s/^panel_id = .*/panel_id = \"$PANEL_ID\"          # set by install.sh --panel-id/" "$CONFIG"
    hostnamectl set-hostname "$PANEL_ID"
    if grep -q '^127\.0\.1\.1' /etc/hosts; then
        sed -i "s/^127\.0\.1\.1.*/127.0.1.1\t$PANEL_ID/" /etc/hosts
    else
        printf '127.0.1.1\t%s\n' "$PANEL_ID" >> /etc/hosts
    fi
fi

echo "==> Creating service user"
if ! id sideshow &>/dev/null; then
    useradd --system --home-dir /var/lib/sideshow --create-home --shell /usr/sbin/nologin sideshow
fi
usermod -aG video,audio,render,input sideshow

echo "==> Installing systemd service"
install -m 0644 "$REPO_DIR/setup/sideshow-player.service" /etc/systemd/system/sideshow-player.service
systemctl daemon-reload
systemctl enable sideshow-player.service
systemctl disable getty@tty1.service || true   # no login prompt flashing on the panel

echo "==> Quieting boot"
sed -i 's/console=tty1/console=tty3/' "$CMDLINE"
for option in quiet logo.nologo vt.global_cursor_default=0 consoleblank=0; do
    grep -qw -- "$option" "$CMDLINE" || sed -i "1 s|\$| $option|" "$CMDLINE"
done
grep -q '^disable_splash=1' "$BOOTCONFIG" || echo 'disable_splash=1' >> "$BOOTCONFIG"

echo
echo "Done. Reboot to start visuals:  sudo reboot"
echo "Logs:                           journalctl -u sideshow-player -f"
