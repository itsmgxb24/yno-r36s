#!/bin/bash
# Installs the aarch64 cross toolchain and the arm64 libraries on Debian trixie (amd64).
# Used by setup-container.sh (local podman container) and by the release workflow on GitHub.
set -e
export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture arm64
apt-get update
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build pkg-config ca-certificates curl git zip \
  crossbuild-essential-arm64 \
  libsdl2-dev:arm64 libpng-dev:arm64 libfreetype-dev:arm64 libharfbuzz-dev:arm64 libpixman-1-dev:arm64 \
  zlib1g-dev:arm64 libfmt-dev:arm64 libicu-dev:arm64 libexpat1-dev:arm64 libmpg123-dev:arm64 \
  libvorbis-dev:arm64 libopusfile-dev:arm64 libsndfile1-dev:arm64 libfluidsynth-dev:arm64 \
  libspeexdsp-dev:arm64 libcurl4-openssl-dev:arm64 libssl-dev:arm64 libinih-dev:arm64 \
  nlohmann-json3-dev
apt-get clean
