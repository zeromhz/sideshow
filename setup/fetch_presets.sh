#!/usr/bin/env bash
# Fetch the projectM preset and texture packs at pinned commits and lay out playlists.
# Usage: setup/fetch_presets.sh <dest-dir>
# Result:
#   <dest>/packs/cream-of-the-crop   the preset pack
#   <dest>/textures                  the texture pack
#   <dest>/presets/default           symlink to the whole pack
#   <dest>/presets/<category>        symlink per pack category (lowercase, spaces -> dashes)
# Safe to re-run. Custom playlists: create more directories (or symlinks) under <dest>/presets.
set -euo pipefail

DEST="${1:?usage: fetch_presets.sh <dest-dir>}"
PRESETS_REPO="https://github.com/projectM-visualizer/presets-cream-of-the-crop.git"
PRESETS_COMMIT="0180df21f5e0bd39b9060cc5de420ed2f1f9e509"
TEXTURES_REPO="https://github.com/projectM-visualizer/presets-milkdrop-texture-pack.git"
TEXTURES_COMMIT="6368812f27bc747b517218fbf89d21d59afce4d9"

fetch_pinned() {
    local repo="$1" commit="$2" dir="$3"
    if [[ -d "$dir/.git" ]] && [[ "$(git -C "$dir" rev-parse HEAD)" == "$commit" ]]; then
        echo "$dir already at ${commit:0:8}"
        return
    fi
    rm -rf "$dir"
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" fetch -q --depth 1 "$repo" "$commit"
    git -C "$dir" checkout -q FETCH_HEAD
}

fetch_pinned "$PRESETS_REPO" "$PRESETS_COMMIT" "$DEST/packs/cream-of-the-crop"
fetch_pinned "$TEXTURES_REPO" "$TEXTURES_COMMIT" "$DEST/textures"

mkdir -p "$DEST/presets"
ln -sfn ../packs/cream-of-the-crop "$DEST/presets/default"
for category in "$DEST/packs/cream-of-the-crop"/*/; do
    name="$(basename "$category")"
    [[ "$name" == "! Transition" ]] && continue
    playlist="$(echo "$name" | tr '[:upper:] ' '[:lower:]-')"
    ln -sfn "../packs/cream-of-the-crop/$name" "$DEST/presets/$playlist"
done

echo "Playlists in $DEST/presets: $(ls "$DEST/presets" | tr '\n' ' ')"
