#!/bin/bash
# Creates the rootless podman container "yno-dev" used to cross-build the R36S client.
# Debian trixie (the same userland as dArkOS) + the aarch64 cross compiler + arm64 -dev libraries.
#
#   r36s/build/setup-container.sh [podman storage dir]
#
# The repository is mounted as /work, build output goes to /out (a host directory, default
# ~/yno-build-out). Pass a storage dir on a big, fast disk (or tmpfs) if ~/.local/share/containers
# is small: the container needs about 3 GB.
#
# Build afterwards:   podman exec yno-dev /work/r36s/build/build.sh
set -e

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${OUT:-$HOME/yno-build-out}"
STORE="$1"
mkdir -p "$OUT"

POD=(podman)
if [ -n "$STORE" ]; then
    mkdir -p "$STORE/root" "$STORE/run" "$STORE/tmp"
    export TMPDIR="$STORE/tmp"
    POD=(podman --root "$STORE/root" --runroot "$STORE/run")
fi

"${POD[@]}" rm -f yno-dev >/dev/null 2>&1 || true
"${POD[@]}" run -d --name yno-dev --security-opt label=disable \
    -v "$REPO":/work -v "$OUT":/out docker.io/library/debian:trixie-slim sleep infinity

"${POD[@]}" exec yno-dev bash /work/r36s/build/install-deps.sh
echo "ready: ${POD[*]} exec yno-dev /work/r36s/build/build.sh   (result: $OUT/yno-r36s)"
