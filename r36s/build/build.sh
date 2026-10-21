#!/bin/bash
# Cross-builds the native YNO client for the R36S (aarch64, Debian trixie userland).
# Run it inside the build container (r36s/build/setup-container.sh) or on any Debian trixie amd64
# machine that has the packages listed there.
#
#   r36s/build/build.sh [extra cmake arguments]
#
# OUT (default /out) receives player-a64/ (the build tree) and yno-r36s (the stripped program).
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/../.." && pwd)"
OUT="${OUT:-/out}"
LIBLCF_COMMIT=6854310c3432e553fd4ae672ce861899c80c3bd0   # the liblcf this was developed against

export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig

# liblcf is built together with the player; fetch it once, pinned
if [ ! -e "$SRC/lib/liblcf" ]; then
    mkdir -p "$SRC/lib"
    git clone https://github.com/EasyRPG/liblcf.git "$SRC/lib/liblcf"
    git -C "$SRC/lib/liblcf" checkout "$LIBLCF_COMMIT"
fi

cmake -S "$SRC" -B "$OUT/player-a64" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HERE/aarch64.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPLAYER_YNO_NATIVE=ON -DPLAYER_BUILD_LIBLCF=ON \
  -DPLAYER_TARGET_PLATFORM=SDL2 \
  -DPLAYER_ENABLE_TESTS=OFF -DBUILD_SHARED_LIBS=OFF \
  -DPLAYER_WITH_WILDMIDI=OFF -DPLAYER_WITH_XMP=OFF -DPLAYER_WITH_LHASA=OFF -DPLAYER_WITH_FLUIDLITE=OFF \
  "$@"
cmake --build "$OUT/player-a64" -j"$(nproc)"
aarch64-linux-gnu-strip -o "$OUT/yno-r36s" "$OUT/player-a64/easyrpg-player"
echo "built: $OUT/yno-r36s"
