# common

This is the shared part of every recomp here.

## What's in here

- The **recompiler** (`tools/recomp/`), the thing that reads a game's ARM binary and writes it back out as C++. It's the same translator for every game; each title just points it at its own binary.
- The **runtime**: the little model of the iPod's CPU and memory that the translated code runs on top of.
- The **renderer**, a from-scratch reimplementation of the iPod's graphics chip in plain software, since these games talk to a GPU that no longer exists. This is the piece that had the most bugs shared between games, which is exactly why it's shared now.
- The **platform layer**: the window, the keyboard and input, the settings window, where saves go, and the bits that read the clock and battery off your actual machine.
- The **build tooling**, including the scripts that make the macOS, Windows, and Switch builds.

The framework interfaces (what a game is allowed to ask the platform to do) live here too, as plain C++ with no trace of the iPod's original calling convention.

## How a game uses it

You don't build this on its own. Each game pulls it in as a subdirectory (`ipod_core`) when you build that game, and compiles it under that game's own rules, so there's nothing to set up here separately. A game reaches the shared code through small forwarding headers at the paths it used to keep its own copies at, which is what let me move things over one file at a time without breaking anything.

The rule for what earns a place in here: if two games differ because of a real, measured fact about their binaries, that stays with each game and gets passed in as a setting. If they only differ because one got fixed and the other didn't, that's not really either game's, and it belongs here.

## Licence

The shared runtime and renderer are GPL-3.0-or-later, like the games. The tools under `tools/` are MIT. See `../LICENSING.md` for the split and what it does and doesn't cover.
