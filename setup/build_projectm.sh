#!/usr/bin/env bash
# Build and install libprojectM + its playlist library at a pinned release.
# Usage: setup/build_projectm.sh <install-prefix> [--gles]
# Safe to re-run: exits early if the pinned version is already installed.
set -euo pipefail

PROJECTM_TAG="v4.1.7"
PREFIX="${1:?usage: build_projectm.sh <install-prefix> [--gles]}"
GLES=OFF
if [[ "${2:-}" == "--gles" ]]; then GLES=ON; fi

VERSION_FILE="$PREFIX/lib/cmake/projectM4/projectM4ConfigVersion.cmake"
if [[ -f "$VERSION_FILE" ]] && grep -q "set(PACKAGE_VERSION \"${PROJECTM_TAG#v}\")" "$VERSION_FILE"; then
    echo "libprojectM ${PROJECTM_TAG} already installed in $PREFIX"
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

git clone --depth 1 --branch "$PROJECTM_TAG" --recurse-submodules --shallow-submodules \
    https://github.com/projectM-visualizer/projectm.git "$WORK/src"

cmake -S "$WORK/src" -B "$WORK/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DENABLE_GLES="$GLES" \
    -DENABLE_PLAYLIST=ON \
    -DENABLE_SYSTEM_PROJECTM_EVAL=OFF \
    -DBUILD_TESTING=OFF

cmake --build "$WORK/build" --parallel "$(getconf _NPROCESSORS_ONLN)"
cmake --install "$WORK/build"
echo "Installed libprojectM ${PROJECTM_TAG} to $PREFIX"
