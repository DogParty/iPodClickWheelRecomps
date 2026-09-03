# Vortex

This is Apple's Vortex from 2006, which is Breakout wrapped around a circle: the bricks ring the outside and you spin your paddle around the rim to keep the ball in play. You spin the wheel to move the paddle, which on a modern machine is the arrow keys or a gamepad.

## The game's files

You bring your own copy, same as every game here. Vortex's folder is `12345`. The first time you open it, the game asks you for that folder (or a zip of it), checks every file against the size and checksum it shipped with, and copies it in beside your saves. If something's wrong or missing it tells you why and asks again. None of those files are in this repo, and I won't help you find or decrypt them. Press Escape to leave a game and it saves and closes cleanly, the way it used to hand the iPod back to its own menu.

## Settings

Open the settings window with ⌘+, on macOS or Ctrl+, on Windows. It has three tabs:

- **General** is the frame rate: 30, 60, or unlocked. It's 30 by default, which is both how the iPod ran it and the rate the game's own physics were written for, so I'd leave it there.
- **Input** lists every action and the key it's bound to, and you rebind by picking a new key from the list. There's a Restore Defaults button.
- **Graphics** is how the small 320x240 picture gets blown up to fill your window, plus a render scale up to 8x. It won't magically sharpen 2006 art, but it cleans up edges and makes the text easier to read.

Everything you change is saved and comes back next launch. Fullscreen is F or F11.

## Building

Because Vortex isn't fully recompiled yet, building it has one extra step Mini Golf doesn't: you generate the C++ from your own copy of the game first. You'll need CMake 3.21 or newer, a C++17 compiler, Python 3.10+, SDL3, and zlib.

First, generate the code from your game binary:

```sh
python3 tools/funcs.py --image "PATH/12345/Executables/vortex_1_1_2563290.bin"
python3 tools/emit.py
```

That reads your copy and writes the translated C++ under `gen/`. It's a translation of the game's own code, so it's never checked in; you make it from your own files. The `.bin` lives inside your `12345` folder under `Executables/`, the same folder the built game asks you for. Skip this and the build will configure and compile fine, then fail to link with a wall of missing symbols.

### macOS

```sh
cmake -B build && cmake --build build
open build/vortex.app
```

You get a real `.app` that carries its own SDL inside it. It isn't signed, so macOS might refuse to open it the first time. If it does, go to System Settings > Privacy & Security and click Allow on the row that mentions Vortex.

### Windows

There's a 64-bit Windows build, the same program as the Mac one, made inside a container so all you actually need is Docker:

```sh
tools/windows-build.sh
```

That leaves `vortex.exe` and the `SDL3.dll` it loads in `build-windows/dist/`. On a real Windows machine with MSVC you can also `cmake -B build` the normal way. I haven't put much time into Windows, so treat it as working but lightly tested.

## Legal

The game belongs to its publisher. None of its code, art, music, or levels are in this repository, and no build contains any of it. What's here is a port: my own host code plus a machine translation of the game logic that you generate from your own copy of the files.

## Licence

GPL-3.0-or-later, like the rest of the ports; the tools under `../common/tools/` are MIT. See `../LICENSING.md` for the split and what it does and doesn't cover.
