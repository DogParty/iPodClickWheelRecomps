# Mini Golf

Mini Golf is the furthest along of the six games here, and the only one that's finished. The whole thing has been recompiled and then decompiled by hand, so there's no machine-translated code left in it at all: every function the iPod ran is now readable C++ with a real name on it. It's also the only one that runs on three platforms instead of two: macOS, Windows, and the Nintendo Switch.

It's the click wheel Mini Golf from 2008: 54 holes across three courses, played by spinning the wheel to line up a shot and clicking to swing. On a modern machine you do the spinning with the arrow keys, a gamepad stick, or a D-pad. It's not quite the same as a real wheel under your thumb, but it plays well once you're used to it. 

## The game's files

Like every game here, you bring your own copy of the files. Mini Golf's folder is `88888`. The first time you open the game it asks you for a zip of that folder, checks every file in it against the size and checksum it shipped with, and keeps it beside your saves from then on. If the zip is wrong or damaged it tells you why and asks again. Nothing in this repo contains those files, and I won't help you find or decrypt them.

## Cheats

There's a Cheats screen under Options now, with five toggles:

- **Unlock courses** makes all three courses selectable no matter how few you've earned
- **No stroke limit** raises the ten-stroke cap to 99
- **No out of bounds** puts a ball that leaves the green back where you played it from. The stroke still counts.
- **Aim guide** has the aim line follow the power meter as it swings, instead of only showing the full reach
- **Ghost trail** faintly draws the path the ball took on your best round of a hole while you line up the next one

A round you play with a rule-changing cheat doesn't count: no best score, no stats, no course unlocked by it. The round still plays out to the end, it just leaves no trace. Ghost trail is the exception, since it only shows you a hole you've already finished, so a round with it on still counts. Ghost trail is in alpha so it might be a little wonky.

## Settings

Open the settings window with ⌘+, on macOS or Ctrl+, on Windows. It has three tabs:

- **General** is the frame rate: 30, 60, or unlocked. It's 30 by default, which is how the iPod ran it.
- **Input** lists every action with the key it's bound to, and you rebind by picking a new key from the list. There's a Restore Defaults button. A gamepad works too, out of the box: the D-pad or left stick turns the wheel, A selects, B is Menu, and so on, all of it rebindable.
- **Graphics** is how the small picture gets blown up to fill your window. Sharp (the default) keeps the pixels square and stops the edges crawling when you resize, Nearest is hard blocks, and Smooth is soft. There's also a render scale up to 8x, which won't magically sharpen 2008 art but does clean up edges and make the text a lot easier to read.

Everything you change is saved and comes back next time you launch. Fullscreen is F or F11.

The one thing I took out is Brightness. It set the iPod's backlight, and nothing this runs on has one to set, so that row has been removed.

## Building

You need CMake 3.21 or newer, a C++17 compiler, Python 3.10+, SDL3, and zlib. And your own copy of the game's folder, same as running it.

### macOS

This is the one I actually develop and play on, so it's the most tested by a wide margin.

```sh
cmake -B build && cmake --build build
open build/minigolf.app
```

You get a real `.app` that carries its own SDL inside it, so you can move it wherever you like and double-click it. 

Note that this game hasn't been signed so macOS might not let you open it. If that's the case, then go to System Settings > Privacy & Security > Click Allow on the section that mentions Mini Golf.

### Windows

There's a 64-bit Windows build, and it's the same program as the Mac one. The easy way to make it is the cross-build script, which does the whole thing inside a container, so all you actually need is Docker:

```sh
tools/windows-build.sh
```

That leaves `minigolf.exe` and the `SDL3.dll` it loads in `build-windows/dist/`. On a real Windows machine with MSVC you can also just `cmake -B build` the normal way. Either way, I haven't put many hours into Windows yet, so treat it as working but lightly tested. 

One thing to note is that Windows supports .m4a background music through its Media Foundation library while homebrew platforms and Linux do not. I guess that's one thing your Windows licence fee went to.

### Nintendo Switch

There's a homebrew build too, a `.nro` the Homebrew Menu can load. Like the Windows build it's done in a container, so Docker is all you need:

```sh
tools/switch-build.sh
```

That gives you `build-switch/minigolf-switch.nro`. Copy it to `sdmc:/switch/minigolf-switch.nro`, and copy your `88888` folder to `sdmc:/switch/minigolf/88888/`, since the Switch build has no file browser to ask for it. Then launch it from the Homebrew Menu.

Big caveat here: I haven't booted my Switch in years so this has never actually run on real hardware. It cross-compiles cleanly and the parts that are plain logic are shared with the desktop build, but the rest is code review and hope. I ran it in an emulator and it seemed to work well. If you try it and it works, I'd genuinely love to hear about it. There's no music on the Switch yet either, and names are still spelled out on the wheel since there's no keyboard for it.

## Legal

The game belongs to its publisher. None of its code, art, music, or levels are in this repository, and no build contains any of it. What's here is a port: my own host code and a hand decompilation of the game logic, which needs your own copy of the files to do anything at all.

## Licence

GPL-3.0-or-later, like the rest of the ports; the tools under `../common/tools/` are MIT. See `../LICENSING.md` for the split and what it does and doesn't cover.
