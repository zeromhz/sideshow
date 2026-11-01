#!/usr/bin/env bash
# Provision a Raspberry Pi 5 (Raspberry Pi OS Lite, Bookworm 64-bit) as a sideshow panel.
# Safe to re-run. Usage: sudo setup/install.sh [--panel-id panel-03] [--wifi "Name" "password"]...
#   --wifi saves a Wi-Fi network (repeatable; "" as the password for an open network). The Pi
#   joins whichever saved network it finds, so save the stage network, a phone hotspot, etc.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_ROOT=/opt/sideshow
CONFIG=/etc/sideshow/config.toml
CMDLINE=/boot/firmware/cmdline.txt
BOOTCONFIG=/boot/firmware/config.txt
PANEL_ID=""
WIFI_NAMES=()
WIFI_PASSWORDS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --panel-id) PANEL_ID="${2:?--panel-id needs a value}"; shift 2 ;;
        --wifi)
            if [[ $# -lt 3 ]]; then echo "--wifi needs a network name and a password (\"\" if open)" >&2; exit 2; fi
            WIFI_NAMES+=("$2"); WIFI_PASSWORDS+=("$3"); shift 3 ;;
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
for i in "${!WIFI_NAMES[@]}"; do
    name="${WIFI_NAMES[$i]}"; password="${WIFI_PASSWORDS[$i]}"
    if [[ -z "$name" || ${#name} -gt 32 ]]; then echo "Wi-Fi name must be 1-32 characters: '$name'" >&2; exit 2; fi
    if [[ -n "$password" && ( ${#password} -lt 8 || ${#password} -gt 63 ) ]]; then
        echo "Wi-Fi password for '$name' must be 8-63 characters (or \"\" for an open network)" >&2; exit 2
    fi
done

echo "==> Installing packages"
apt-get update
apt-get install -y build-essential cmake git pkg-config \
    libsdl2-dev liblo-dev libgles-dev libegl-dev avahi-daemon \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev

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

install -d -m 0755 "$INSTALL_ROOT/videos"
"$REPO_DIR/setup/install_isf_examples.sh" "$INSTALL_ROOT/isf"

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

echo "==> Tuning for stage use"
# Wi-Fi power saving delays incoming OSC packets by up to a few hundred milliseconds.
if [[ -d /etc/NetworkManager/conf.d ]]; then
    printf '[connection]\nwifi.powersave = 2\n' > /etc/NetworkManager/conf.d/sideshow-wifi-powersave.conf
fi
# Background package-list downloads and man-page indexing cause random CPU and SD-card bursts.
# (Raspberry Pi OS Lite does not install updates automatically; update by hand between shows.)
systemctl disable --now apt-daily.timer apt-daily-upgrade.timer man-db.timer 2>/dev/null || true

echo "==> Network access"
# Changes saved profiles only; the network you are connected over stays up.
if command -v nmcli >/dev/null && systemctl is-active --quiet NetworkManager; then
    # Saved Wi-Fi networks (--wifi). Existing ones are updated in place, not re-created.
    for i in "${!WIFI_NAMES[@]}"; do
        name="${WIFI_NAMES[$i]}"; password="${WIFI_PASSWORDS[$i]}"; profile="sideshow-wifi-$name"
        if ! nmcli -t -e no -f NAME connection show | grep -Fxq -- "$profile"; then
            nmcli connection add type wifi con-name "$profile" ifname wlan0 ssid "$name" >/dev/null ||
                { echo "    could not save Wi-Fi network '$name'" >&2; continue; }
        fi
        if [[ -n "$password" ]]; then
            nmcli connection modify "$profile" wifi.ssid "$name" wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$password"
        else
            nmcli connection modify "$profile" wifi.ssid "$name" remove wifi-sec 2>/dev/null || true
        fi
        nmcli connection modify "$profile" connection.autoconnect yes
        echo "    saved Wi-Fi network: $name"
    done
    # Keep retrying every saved Wi-Fi network forever. NetworkManager's default gives up after
    # four attempts for about five minutes, so a panel booted before its hotspot or stage
    # network was up could sit offline.
    # (Name last and unescaped, so names containing ':' stay whole.)
    while IFS=: read -r type profile; do
        [[ "$type" == "802-11-wireless" ]] || continue
        nmcli connection modify "$profile" connection.autoconnect-retries 0 || true
    done < <(nmcli -t -e no -f TYPE,NAME connection show)
    # Ethernet: a router's DHCP first; without one (a cable straight to a laptop), fall back to a
    # self-assigned 169.254.x.x address after 20 s, so ssh <user>@<panel-id>.local still works.
    if nmcli -t -f DEVICE device | grep -Fxq eth0; then
        if ! nmcli -t -e no -f NAME connection show | grep -Fxq sideshow-wired; then
            nmcli connection add type ethernet con-name sideshow-wired ifname eth0 >/dev/null || true
        fi
        nmcli connection modify sideshow-wired ipv4.method auto ipv4.dhcp-timeout 20 \
            connection.autoconnect-priority 10 connection.autoconnect-retries 1 || true
        if ! nmcli -t -e no -f NAME connection show | grep -Fxq sideshow-wired-direct; then
            nmcli connection add type ethernet con-name sideshow-wired-direct ifname eth0 >/dev/null || true
        fi
        nmcli connection modify sideshow-wired-direct ipv4.method link-local ipv6.method link-local \
            connection.autoconnect-priority 0 || true
    fi
else
    echo "    NetworkManager is not running; skipping Wi-Fi and Ethernet setup"
fi
systemctl enable --now avahi-daemon 2>/dev/null || true   # <panel-id>.local names

echo "==> Quieting boot"
sed -i 's/console=tty1/console=tty3/' "$CMDLINE"
for option in quiet logo.nologo vt.global_cursor_default=0 consoleblank=0; do
    grep -qw -- "$option" "$CMDLINE" || sed -i "1 s|\$| $option|" "$CMDLINE"
done
grep -q '^disable_splash=1' "$BOOTCONFIG" || echo 'disable_splash=1' >> "$BOOTCONFIG"

echo
echo "Done. Reboot to start visuals:  sudo reboot"
echo "Logs:                           journalctl -u sideshow-player -f"
