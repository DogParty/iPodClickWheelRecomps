# Third-party licences

What a released artifact has to carry beside the binary, because the binary contains or loads it.

* `SDL3-LICENSE.txt` — SDL3, under the zlib licence. Every windowed build links it: as
  `SDL3.dll` beside the .exe on Windows, and inside `Contents/Frameworks` in the macOS .app. The
  zlib licence asks that the notice not be removed from a distribution, so it ships in every
  artifact that contains SDL (`common/tools/release.sh` copies it in).
* `LIBCXX-LICENSE.txt` — libc++ and libc++abi, under the Apache License 2.0 with LLVM
  Exceptions. Only the Android artifact carries it, and only because only that build links the
  C++ standard library *into* the program: there is no `libc++_shared.so` beside it to load, so
  a copy of the library is inside `libmain.so` and its licence has to be in the download. Taken
  from the NDK's own `NOTICE.toolchain`, which states it for the whole toolchain; what is here
  is the part that covers what actually ships.

Nothing else is vendored. These are the libraries a build links, none of whose source is in this
tree:

| Library | Where it is linked | Licence | Travels with a release? |
|---|---|---|---|
| SDL3 | every windowed build | zlib | yes, as `SDL3-LICENSE.txt` |
| zlib | the zip reader and the CRC-32 of every installed file | zlib | no: taken from the system (the macOS SDK, MinGW's static build) |
| libnx | the Switch build only | ISC | no: statically linked from devkitPro, whose notice ships with the toolchain |
| libc++ | the C++ standard library | Apache-2.0 with LLVM Exceptions | Android only, as `LIBCXX-LICENSE.txt`: that build links it in, while macOS uses the system's and MinGW's libstdc++ has the GCC Runtime Library Exception |
| libmediandk | the Android build's music decoder | part of the platform | no: a system library, loaded from the device like any other |

The game's own files are not here and are not in any artifact: they are the player's own copy,
taken off their own iPod. See any title's README ("The game's files"), and `../../LICENSING.md`
for what this project's own licences do and do not cover.
