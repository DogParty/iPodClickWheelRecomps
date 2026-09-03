# Cubis 2

It's the click wheel version of Cubis 2 from 2007, a puzzle game where you fire cubes into an isometric grid and match three of a color to clear them. You spin the wheel to aim and click to fire, which on a modern machine is the arrow keys or a gamepad and the select key.

There's a fun bug worth mentioning: for a while every cube on the board came out grey, because the shared renderer was ignoring a colour register that Cubis leans on more than the other games do. Fixing it in one place fixed a few of the other titles too, which is exactly why they all share a renderer :)

## The game's files

You bring your own copy, same as every game here. Cubis 2's folder is `99999`. The first time you open it, it asks you for that folder (or a zip of it), checks every file against the size and checksum it shipped with, and copies it in beside your saves. If something's wrong or missing it tells you why and asks again. None of those files are in this repo, and I won't help you find or decrypt them. The game writes two files of its own, `cubisgame.dat` and `cubissave.dat`, and those stay with your copy.

## Settings

Open the settings window with ⌘+, on macOS or Ctrl+, on Windows. It has three tabs:

- **General** is the frame rate: 30, 60, or unlocked. It's 30 by default, which is how the iPod ran it.
- **Input** lists every action and the key it's bound to, and you rebind by picking a new key from the list. There's a Restore Defaults button.
- **Graphics** is how the small 320x240 picture gets blown up to fill your window, plus a render scale up to 8x. It won't magically sharpen 2007 art, but it cleans up edges and makes the text easier to read.

Everything you change is saved and comes back next launch. Fullscreen is F or F11.

## Building

Because Cubis 2 isn't fully recompiled yet, building it has one extra step Mini Golf doesn't: you generate the C++ from your own copy of the game first. You'll need CMake 3.21 or newer, a C++17 compiler, Python 3.10+, SDL3, and zlib.

First, generate the code from your game binary:

```sh
python3 tools/funcs.py --image "PATH/99999/Executables/Cubis2_1_1_2563292.bin"
python3 tools/emit.py
```

That reads your copy and writes the translated C++ under `gen/`. It's a translation of the game's own code, so it's never checked in; you make it from your own files. The `.bin` lives inside your `99999` folder under `Executables/`, the same folder the built game asks you for. Skip this and the build will configure and compile fine, then fail to link with a wall of missing symbols.

### macOS

```sh
cmake -B build && cmake --build build
open build/cubis.app
```

You get a real `.app` that carries its own SDL inside it. It isn't signed, so macOS might refuse to open it the first time. If it does, go to System Settings > Privacy & Security and click Allow on the row that mentions Cubis 2.

### Windows

There's a 64-bit Windows build, the same program as the Mac one, made inside a container so all you actually need is Docker:

```sh
tools/windows-build.sh
```

That leaves `cubis.exe` and the `SDL3.dll` it loads in `build-windows/dist/`. On a real Windows machine with MSVC you can also `cmake -B build` the normal way. I haven't put much time into Windows, so treat it as working but lightly tested.

## Legal

The game belongs to its publisher. None of its code, art, music, or levels are in this repository, and no build contains any of it. What's here is a port: my own host code plus a machine translation of the game logic that you generate from your own copy of the files.

## Licence

GPL-3.0-or-later, like the rest of the ports; the tools under `../common/tools/` are MIT. See `../LICENSING.md` for the split and what it does and doesn't cover.
