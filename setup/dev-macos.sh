#!/usr/bin/env bash
# One-time macOS development setup: Homebrew deps, projectM into ./deps/install,
# preset and texture packs into ./deps.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

brew install cmake pkg-config sdl2 liblo ffmpeg
"$REPO_DIR/setup/build_projectm.sh" "$REPO_DIR/deps/install"
"$REPO_DIR/setup/fetch_presets.sh" "$REPO_DIR/deps"

mkdir -p "$REPO_DIR/deps/videos"

cat <<EOF

Dev setup complete. Build and run the player with:
  cmake -S . -B build -DSIDESHOW_BUILD_PLAYER=ON -DCMAKE_PREFIX_PATH="$REPO_DIR/deps/install"
  cmake --build build -j
  ./build/sideshow-player --config setup/config.dev.toml
EOF
