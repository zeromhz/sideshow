#!/usr/bin/env bash
# Fetch the projectM-presets-rpi5 preset pack and the milkdrop texture pack at
# pinned commits, and lay out playlists.
# Usage: setup/fetch_presets.sh <dest-dir>
# Result:
#   <dest>/packs/projectM-presets-rpi5    the preset pack
#   <dest>/packs/milkdrop-texture-pack    the texture pack (source checkout)
#   <dest>/textures                       real directory of texture files, copied
#                                          from both packs (projectM does not
#                                          follow nested directory symlinks)
#   <dest>/presets/default                symlink to the whole rpi5 preset pack
#   <dest>/presets/<category>             symlink per pack category (lowercase, spaces -> dashes)
# Safe to re-run. Custom playlists: create more directories (or symlinks) under
# <dest>/presets; a real directory there is never touched by this script.
set -euo pipefail

DEST="${1:?usage: fetch_presets.sh <dest-dir>}"
PRESETS_REPO="https://github.com/mickabrig7/projectM-presets-rpi5.git"
PRESETS_COMMIT="60eaf82081cd247c466829a47713eeb19dc0d1fa"
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

fetch_pinned "$PRESETS_REPO" "$PRESETS_COMMIT" "$DEST/packs/projectM-presets-rpi5"
fetch_pinned "$TEXTURES_REPO" "$TEXTURES_COMMIT" "$DEST/packs/milkdrop-texture-pack"

# Migrate away from the old layout: cream-of-the-crop pack, and textures as a
# git checkout instead of a plain directory of copied files. (An old textures/
# git checkout is just a stale build of the same directory, so it's fine for
# the rebuild below to replace it wholesale.)
rm -rf "$DEST/packs/cream-of-the-crop"

# projectM's texture search is recursive but does not follow nested directory
# symlinks, so <dest>/textures must be a real directory of real files. Rebuild
# it from scratch each run (rather than merging in new files with `cp -n`) so
# stale textures don't pile up when the pinned commits are bumped, and so a
# real I/O failure (full disk, permissions) isn't silently swallowed the way
# `cp -n`'s "file already exists" non-error would otherwise force us to.
# Milkdrop textures go in first, then the rpi5 pack's own textures overwrite
# any same-named file: the rpi5 presets were benchmarked against its own
# textures (e.g. seaweed.jpg differs between the two packs), so its version
# must win.
rm -rf "$DEST/textures.tmp"
mkdir -p "$DEST/textures.tmp"
cp -R "$DEST/packs/milkdrop-texture-pack/textures"/. "$DEST/textures.tmp/"
cp -R "$DEST/packs/projectM-presets-rpi5/textures"/. "$DEST/textures.tmp/"
rm -rf "$DEST/textures"
mv "$DEST/textures.tmp" "$DEST/textures"

mkdir -p "$DEST/presets"

# Rebuild playlist symlinks: remove only symlinks that point into ../packs/,
# never real directories (those are user-curated playlists).
if [[ -d "$DEST/presets" ]]; then
    while IFS= read -r -d '' link; do
        target="$(readlink "$link")"
        case "$target" in
            ../packs/*) rm -f "$link" ;;
        esac
    done < <(find "$DEST/presets" -mindepth 1 -maxdepth 1 -type l -print0)
fi

ln -sfn ../packs/projectM-presets-rpi5/presets "$DEST/presets/default"

shopt -s nullglob
for category in "$DEST/packs/projectM-presets-rpi5/presets/creamofthecrop"/*/; do
    name="$(basename "$category")"
    [[ "$name" == "! Transition" ]] && continue
    playlist="$(echo "$name" | tr '[:upper:] ' '[:lower:]-')"
    ln -sfn "../packs/projectM-presets-rpi5/presets/creamofthecrop/$name" "$DEST/presets/$playlist"
done

ln -sfn ../packs/projectM-presets-rpi5/presets/isoscelesmashup "$DEST/presets/isosceles"
ln -sfn ../packs/projectM-presets-rpi5/presets/BLTC "$DEST/presets/bltc"
ln -sfn ../packs/projectM-presets-rpi5/presets/EnD "$DEST/presets/en-d"

echo "Playlists in $DEST/presets: $(ls "$DEST/presets" | tr '\n' ' ')"
