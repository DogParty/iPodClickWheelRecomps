#!/bin/sh
# Build the 64-bit Windows version (holdem.exe): the shared script, told which title this is.
#
#   tools/windows-build.sh [clean]
#
# See ../../common/tools/windows-build.sh for what it does and needs (Docker, and nothing else).
# The result is build-windows/dist/: holdem.exe beside the SDL3.dll it loads; see README.md
# ("Windows").
exec "$(dirname "$0")/../../common/tools/windows-build.sh" "$(dirname "$0")/.." holdem "$@"
