#!/bin/sh
# Build a title's 64-bit Windows version (its .exe) in a container with MinGW-w64.
#
#   common/tools/windows-build.sh <title-dir> <exe-name> [clean]
#
# Every title's tools/windows-build.sh calls this with its own directory and the name of its
# SDL3 executable, so the toolchain, the image and the steps live in one place. Nothing
# Windows-flavoured is installed on the machine, and nothing needs to be: the script builds an
# Ubuntu image with the MinGW-w64 cross-compiler, its zlib, and SDL3's own mingw development
# package, and runs the build in it against the working tree. Docker (or a drop-in that answers
# to `docker`) is the only thing needed locally. The toolchain file is mingw-w64.cmake here.
#
# The result is <title-dir>/build-windows/dist/: the .exe beside the SDL3.dll it loads.
set -eu

title=$(cd "${1:?usage: windows-build.sh <title-dir> <exe-name> [clean]}" && pwd)
exe=${2:?usage: windows-build.sh <title-dir> <exe-name> [clean]}
root=$(dirname "$title")  # the repository root: CMakeLists.txt takes the shared code from ../common
image=ipod-recomp-windows-dev
sdl3_version=3.4.16

if ! command -v docker > /dev/null 2>&1; then
    echo "windows-build.sh: docker is not installed, and the toolchain lives in a container" >&2
    exit 2
fi
if ! docker info > /dev/null 2>&1; then
    echo "windows-build.sh: the docker daemon is not running" >&2
    exit 2
fi

if ! docker image inspect "$image" > /dev/null 2>&1; then
    echo "windows-build.sh: building the $image image (ubuntu + mingw-w64 + zlib + SDL3 $sdl3_version)"
    docker build -q -t "$image" --build-arg SDL3_VERSION="$sdl3_version" - <<'DOCKERFILE'
FROM ubuntu:24.04
ARG SDL3_VERSION
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates cmake curl g++-mingw-w64-x86-64-posix libz-mingw-w64-dev ninja-build \
    && rm -rf /var/lib/apt/lists/*
# SDL3's mingw development package carries the headers, the import library, the CMake package
# and the DLL for x86_64-w64-mingw32; only that flavour is kept.
RUN curl -fsSL -o /tmp/sdl3.tar.gz \
        "https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}/SDL3-devel-${SDL3_VERSION}-mingw.tar.gz" \
    && mkdir -p /tmp/sdl3 && tar -xzf /tmp/sdl3.tar.gz -C /tmp/sdl3 --strip-components=1 \
    && mv /tmp/sdl3/x86_64-w64-mingw32 /opt/sdl3 && rm -rf /tmp/sdl3 /tmp/sdl3.tar.gz
DOCKERFILE
fi

if [ "${3:-}" = "clean" ]; then
    rm -rf "$title/build-windows"
fi

docker run --rm -v "$root":/work -w "/work/$(basename "$title")" "$image" sh -c "
    set -e
    cmake -B build-windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=../common/tools/mingw-w64.cmake
    cmake --build build-windows -j\"\$(nproc)\"
    mkdir -p build-windows/dist
    cp build-windows/$exe.exe /opt/sdl3/bin/SDL3.dll build-windows/dist/
"

ls -l "$title/build-windows/dist"
