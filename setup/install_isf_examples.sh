#!/usr/bin/env bash
# Install missing bundled examples, preserving every existing user entry.
set -euo pipefail
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:?usage: install_isf_examples.sh SHADER_ROOT}"
for file in examples/plasma.fs examples/spectrum.fs examples/feedback.fs \
            fractal/julia.fs fractal/dive.fs fractal/kleinian.fs \
            fractal/phoenix.fs fractal/nova.fs fractal/newton.fs \
            fractal/burning_ship.fs fractal/multibrot.fs fractal/nebula.fs; do
    install -d -m 0755 "$DEST/$(dirname "$file")"
    if [[ ! -e "$DEST/$file" && ! -L "$DEST/$file" ]]; then
        install -m 0644 "$REPO_DIR/assets/isf/$file" "$DEST/$file"
    fi
done
if [[ ! -e "$DEST/LICENSE.examples" && ! -L "$DEST/LICENSE.examples" ]]; then
    install -m 0644 "$REPO_DIR/assets/isf/LICENSE" "$DEST/LICENSE.examples"
fi
