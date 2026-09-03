# Lost

The iPod game based on the TV show LOST, and it maps out to the first season: you're Jack, working through the island a chapter at a time. The controls are the unusual part. On the iPod you turned the wheel to move a menu, and you rested a finger on one side of the wheel to walk, and those are two different gestures. Here the arrow keys walk (hold one to keep going) and comma and period scroll the menus. The game teaches you this itself on the first screen.

## The game's files

Same deal as every game here: you bring your own copy. Lost's folder is `1B200`. The first time you open it, it asks you for that folder (or a zip of it), checks every file against the size and checksum it shipped with, and copies it in beside your saves. If something's wrong or missing it tells you why and asks again. None of those files are in this repo, and I won't help you find or decrypt them.

## Cheats

There's one so far, on its own tab in Settings:

- **Unlock all chapters** opens up all of the chapters in Play > Select Chapter, from The Arrival through The Escape, instead of only the ones you've reached. Pick any of them and the game loads it.

It doesn't touch your save at all. It just unhides the menu rows the game keeps hidden, every frame, and the moment you turn it off they're hidden again. Nothing you unlock this way gets written anywhere, which is partly on purpose and partly because the game checksums its saves in a way I can't reproduce yet.

## Settings

Open the settings window with ⌘+, on macOS or Ctrl+, on Windows. Lost has four tabs:

- **General** is the frame rate: 30, 60, or unlocked. It's 30 by default, which is how the iPod ran it.
- **Input** lists every action and the key it's bound to, and you rebind by picking a new key from the list. There's a Restore Defaults button. Since walking and scrolling are different gestures on the wheel, the arrows are the walking keys and comma/period are the scroll, but you can swap any of it here.
- **Graphics** is the scaling (how the small 320x240 picture gets blown up to fill your window) plus a render scale up to 4x. There's also a setting that redraws the dialogue text at your screen's resolution instead of the iPod's, which makes it a lot sharper. It's off by default and only does anything above 1x.
- **Cheats** is the tab above.

Everything you change is saved and comes back next launch.

## Building

Because Lost isn't fully recompiled yet, building it has one extra step Mini Golf doesn't: you generate the C++ from your own copy of the game first. You'll need CMake 3.21 or newer, a C++17 compiler, Python 3.10+, SDL3, and zlib.

First, generate the code from your game binary:

```sh
python3 tools/funcs.py --image "PATH/1B200/Executables/Lost_1_1_2917525.bin"
python3 tools/emit.py
```

That reads your copy and writes the translated C++ under `gen/`. It's a translation of the game's own code, so it's never checked in; you make it from your own files. The `.bin` lives inside your `1B200` folder under `Executables/`, the same folder the built game asks you for. Skip this and the build will configure and compile fine, then fail to link with a wall of missing symbols.

### macOS

```sh
cmake -B build && cmake --build build
open build/lost.app
```

You get a real `.app` that carries its own SDL inside it. It isn't signed, so macOS might refuse to open it the first time. If it does, go to System Settings > Privacy & Security and click Allow on the row that mentions Lost.

### Windows

There's a 64-bit Windows build, the same program as the Mac one, made inside a container so all you actually need is Docker:

```sh
tools/windows-build.sh
```

That leaves `lost.exe` and the `SDL3.dll` it loads in `build-windows/dist/`. On a real Windows machine with MSVC you can also `cmake -B build` the normal way. I haven't put much time into Windows, so treat it as working but lightly tested. Windows does play the `.m4a` background music through its Media Foundation library, same as macOS, which the homebrew builds and Linux don't.

## Legal

The game belongs to its publisher. None of its code, art, music, or levels are in this repository, and no build contains any of it. What's here is a port: my own host code plus a machine translation of the game logic that you generate from your own copy of the files.

## Licence

GPL-3.0-or-later, like the rest of the ports; the tools under `../common/tools/` are MIT. See `../LICENSING.md` for the split and what it does and doesn't cover.
