# Third-party licences

What a released artifact has to carry beside the binary, because the binary contains or loads it.

* `SDL3-LICENSE.txt` — SDL3, under the zlib licence. Every windowed build links it: as
  `SDL3.dll` beside the .exe on Windows, and inside `Contents/Frameworks` in the macOS .app. The
  zlib licence asks that the notice not be removed from a distribution, so it ships in every
  artifact that contains SDL (`common/tools/release.sh` copies it in).

Nothing else is vendored. These are the libraries a build links, none of whose source is in this
tree:

| Library | Where it is linked | Licence | Travels with a release? |
|---|---|---|---|
| SDL3 | every windowed build | zlib | yes, as `SDL3-LICENSE.txt` |
| zlib | the zip reader and the CRC-32 of every installed file | zlib | no: taken from the system (the macOS SDK, MinGW's static build) |
| libnx | the Switch build only | ISC | no: statically linked from devkitPro, whose notice ships with the toolchain |

The game's own files are not here and are not in any artifact: they are the player's own copy,
taken off their own iPod. See any title's README ("The game's files"), and `../../LICENSING.md`
for what this project's own licences do and do not cover.
