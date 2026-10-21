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

"${POD[@]}" exec yno-dev bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture arm64
apt-get update
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build pkg-config ca-certificates curl git \
  crossbuild-essential-arm64 \
  libsdl2-dev:arm64 libpng-dev:arm64 libfreetype-dev:arm64 libharfbuzz-dev:arm64 libpixman-1-dev:arm64 \
  zlib1g-dev:arm64 libfmt-dev:arm64 libicu-dev:arm64 libexpat1-dev:arm64 libmpg123-dev:arm64 \
  libvorbis-dev:arm64 libopusfile-dev:arm64 libsndfile1-dev:arm64 libfluidsynth-dev:arm64 \
  libspeexdsp-dev:arm64 libcurl4-openssl-dev:arm64 libssl-dev:arm64 libinih-dev:arm64 \
  nlohmann-json3-dev
apt-get clean
'
echo "ready: ${POD[*]} exec yno-dev /work/r36s/build/build.sh   (result: $OUT/yno-r36s)"
