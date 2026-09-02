#!/bin/sh
# Build the Nintendo Switch homebrew build (a .nro) in devkitPro's own container.
#
#   tools/switch-build.sh [clean]
#
# The toolchain is devkitA64 with libnx — HorizonOS homebrew, loaded by the Homebrew Menu. None of
# it is installable without root on this machine, and none of it needs to be: devkitPro publish a
# container image with the lot, and the build runs there against this working tree. Docker (or a
# drop-in that answers to `docker`) is the only thing needed locally.
#
# The result is build-switch/minigolf-switch.nro. See README.md for what to copy where.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
image=minigolf-switch-dev

if ! command -v docker > /dev/null 2>&1; then
    echo "switch-build.sh: docker is not installed, and the toolchain lives in a container" >&2
    exit 2
fi
if ! docker info > /dev/null 2>&1; then
    echo "switch-build.sh: the docker daemon is not running" >&2
    exit 2
fi

# devkitPro's image plus zlib, which src/gamedata/ needs and the base image does not carry.
if ! docker image inspect "$image" > /dev/null 2>&1; then
    echo "switch-build.sh: building the $image image (devkitpro/devkita64 + switch-zlib)"
    docker build -q -t "$image" - <<'DOCKERFILE'
FROM devkitpro/devkita64:latest
RUN dkp-pacman -Sy --noconfirm switch-zlib && dkp-pacman -Scc --noconfirm
DOCKERFILE
fi

if [ "${1:-}" = "clean" ]; then
    rm -rf "$here/build-switch"
fi

docker run --rm -v "$here":/work -w /work "$image" sh -lc '
    set -e
    cmake -B build-switch -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake -DMINIGOLF_SDL3=OFF
    cmake --build build-switch -j"$(nproc)"
'

ls -l "$here/build-switch/minigolf-switch.nro"
