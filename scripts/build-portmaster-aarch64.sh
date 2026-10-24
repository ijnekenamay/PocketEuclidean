#!/usr/bin/env bash
# Cross-compiles the PortMaster build (Linux AArch64) with zig as the C/C++
# toolchain. Run from Linux or WSL; no root or system cross-compiler needed.
#
#   scripts/build-portmaster-aarch64.sh
#
# Environment overrides:
#   ZIG      path to the zig executable   (default: ~/tools/zig/zig, then PATH)
#   GLIBC    minimum glibc to target      (default: 2.28)
#   SDL_VER  SDL2 source release to build (default: 2.32.10)
#   OUT      work/output directory        (default: ~/pocket-portmaster-build)
#
# SDL2 is built here only so the app can be linked and its headers used.
# The device's own libSDL2 is used at run time.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZIG="${ZIG:-$HOME/tools/zig/zig}"
command -v "$ZIG" >/dev/null 2>&1 || ZIG="$(command -v zig)"
GLIBC="${GLIBC:-2.28}"
SDL_VER="${SDL_VER:-2.32.10}"
OUT="${OUT:-$HOME/pocket-portmaster-build}"
TARGET="aarch64-linux-gnu.${GLIBC}"

mkdir -p "$OUT/bin"

# CMake needs single-executable compilers, so wrap "zig cc" / "zig c++".
printf '#!/bin/sh\nexec "%s" cc -target %s -mno-outline-atomics "$@"\n' "$ZIG" "$TARGET" > "$OUT/bin/zigcc"
printf '#!/bin/sh\nexec "%s" c++ -target %s -mno-outline-atomics "$@"\n' "$ZIG" "$TARGET" > "$OUT/bin/zigcxx"
printf '#!/bin/sh\nexec "%s" ar "$@"\n' "$ZIG" > "$OUT/bin/zigar"
printf '#!/bin/sh\nexec "%s" ranlib "$@"\n' "$ZIG" > "$OUT/bin/zigranlib"
chmod +x "$OUT"/bin/*

TOOLCHAIN_ARGS=(
  -G Ninja
  -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/toolchains/linux-aarch64.cmake"
  -DCMAKE_C_COMPILER="$OUT/bin/zigcc"
  -DCMAKE_CXX_COMPILER="$OUT/bin/zigcxx"
  -DCMAKE_AR="$OUT/bin/zigar"
  -DCMAKE_RANLIB="$OUT/bin/zigranlib"
)

SDL_PREFIX="$OUT/sdl2-aarch64"
if [ ! -f "$SDL_PREFIX/lib/cmake/SDL2/sdl2-config.cmake" ]; then
  echo "== Building SDL2 $SDL_VER for linking =="
  # zig puts its compiler-rt into the SDL shared library and exports it. The
  # app would then bind __aarch64_*/__divti3 to that copy instead of embedding
  # its own, and the device's libSDL2 does not provide them. Export SDL_* only.
  printf '{ global: SDL_*; local: *; };\n' > "$OUT/sdl2.map"
  if [ ! -d "$OUT/SDL2-$SDL_VER" ]; then
    curl -fL -o "$OUT/sdl2.tar.gz" \
      "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz"
    tar -xzf "$OUT/sdl2.tar.gz" -C "$OUT"
  fi
  cmake -S "$OUT/SDL2-$SDL_VER" -B "$OUT/sdl2-build" "${TOOLCHAIN_ARGS[@]}" \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--version-script=$OUT/sdl2.map" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_TESTS=OFF
  cmake --build "$OUT/sdl2-build"
  cmake --install "$OUT/sdl2-build"
fi

echo "== Building Pocket Euclidean ($TARGET) =="
cmake -S "$REPO" -B "$OUT/app-build" "${TOOLCHAIN_ARGS[@]}" \
  -DPORTMASTER_BUILD=ON -DPOCKET_BUILD_APP=ON \
  -DCMAKE_PREFIX_PATH="$SDL_PREFIX" -DCMAKE_FIND_ROOT_PATH="$SDL_PREFIX"
cmake --build "$OUT/app-build"

BIN="$(ls "$OUT"/app-build/pocketeuclidean.* | head -n 1)"
echo "== Built: $BIN =="
file "$BIN" || true

# The device only provides libSDL2/libc/libm/libpthread/libdl. Any other
# undefined symbol (e.g. __aarch64_* outline-atomics helpers) fails at launch.
if command -v nm >/dev/null 2>&1 && nm -D --undefined-only "$BIN" | grep -q ' __aarch64_'; then
  echo "ERROR: binary references __aarch64_* helper symbols" >&2
  exit 1
fi

# Assemble the PortMaster package: copy the skeleton, add the binary, zip it.
# Zip entries keep Unix permissions so the launch script stays executable.
DIST="$OUT/dist"
rm -rf "$DIST" && mkdir -p "$DIST"
cp -r "$REPO/packaging/portmaster/pocketeuclidean" "$DIST/pocketeuclidean"
cp "$BIN" "$DIST/pocketeuclidean/pocketeuclidean/"
sed -i 's/\r$//' "$DIST/pocketeuclidean/Pocket Euclidean.sh"
chmod +x "$DIST/pocketeuclidean/Pocket Euclidean.sh" \
         "$DIST/pocketeuclidean/pocketeuclidean/$(basename "$BIN")"
python3 - "$DIST" <<'PY'
import os, sys, zipfile
dist = sys.argv[1]
archive = os.path.join(dist, "pocketeuclidean.zip")
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, _, files in os.walk(os.path.join(dist, "pocketeuclidean")):
        for name in sorted(files):
            path = os.path.join(root, name)
            info = zipfile.ZipInfo.from_file(path, os.path.relpath(path, dist))
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as f:
                zf.writestr(info, f.read())
print("== Package:", archive)
PY
