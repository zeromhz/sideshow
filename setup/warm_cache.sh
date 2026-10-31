#!/usr/bin/env bash
# Fill Mesa's on-disk shader cache for a MilkDrop playlist, so preset switches during a show skip
# shader compilation (most of their hitch). Loads every preset of the playlist once at the panel's
# real resolution, then lists the slowest. The player is stopped while this runs.
# Usage: sudo setup/warm_cache.sh [playlist]   (default: the configured [presets] playlist)
# Re-run after Mesa or projectM upgrades. The full 8,000-preset "default" pack takes a while;
# warming the playlists you actually show is quicker.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "run with sudo" >&2
    exit 1
fi

systemctl stop sideshow-player
trap 'systemctl start sideshow-player' EXIT

# Same user, groups and environment as setup/sideshow-player.service.
systemd-run --quiet --wait --pipe --collect \
    --uid=sideshow --gid=sideshow \
    -p SupplementaryGroups="video audio render input" \
    -E HOME=/var/lib/sideshow -E SDL_VIDEODRIVER=kmsdrm -E SDL_AUDIODRIVER=alsa \
    -E MESA_SHADER_CACHE_MAX_SIZE=2G \
    /opt/sideshow/bin/sideshow-player --config /etc/sideshow/config.toml --warm-cache ${1:+"$1"}
