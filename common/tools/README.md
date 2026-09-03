# Shared tooling

`recomp/` is the ARM-to-C++ recompiler: the image reader, the instruction decoder, the
control-flow walk, the C++ writer, and the function table that ties them together. A title's own
`tools/` drives it — `funcs.py` seeds the function table, `emit.py` runs the generator — and adds
`../common/tools` to `sys.path` to import it.

## The Windows build

`windows-build.sh <title-dir> <exe-name> [clean]` builds a title's 64-bit Windows executable in
a container with MinGW-w64, and `mingw-w64.cmake` beside it is the toolchain file it uses. Every
title's own `tools/windows-build.sh` is a wrapper that names itself to this one, so the image,
the toolchain and the steps live here once; a title's README ("Windows") says what comes out
and what it needs. Docker is the only requirement on the machine.
