# Mini Golf — native recompilation and decompilation

A port of the iPod click-wheel game *Mini Golf* (`Minigolf_1_1_2563296.bin`, 2008) to modern
machines. The game is first **statically recompiled** — every ARM function becomes a C++ function
that runs the same instructions on a small CPU-state struct — so it runs natively from day one.
Functions are then **hand-decompiled** one at a time into real, named, readable C++, each swap
proven against the recompiled version. The iPod's application frameworks (OpenGL ES, audio,
file I/O, click wheel) are reimplemented as a host library, `libeapp`, behind a set of plain
C++ interfaces (`src/framework/`) that the game calls directly — no registers, no ordinals.

The goal is source that people can read: every function named for what it does, every
non-obvious line explained, every behaviour traceable to where it was established. `PLAN.md`
opens with the code-quality rules that bind this project; read them before contributing.

## Layout

```
PLAN.md                  the plan of record, with the quality rules and the schedule
README.md                this file
CMakeLists.txt           build; targets `minigolf` (SDL3) and `minigolf-headless` (no window)
tools/
  funcs.py               merge Ghidra + live coverage into gen/funcs.json (the function table)
  emit.py                ARM → C++ static recompiler — now only for the pure recompilation
                         (build/gen-pure/)
  recomp/                the Python package behind those scripts (image, functions, decode, emit)
  DumpFuncs.java         Ghidra headless post-script that produced analysis/ghidra/
src/
  runtime/               guest CPU state and memory, the frame pump, the debugging switches
  platform/save_store.h  where saved games go — each platform's own choice, one file per save
                         beside the game by default
  platform/input_bindings.h  the seven things a player can do, and which input does each; the
                         portable half of rebinding, shared by every platform
  framework/             what the game may ask the platform to do, one header and namespace each:
                         graphics.h (gfx), audio.h (audio), storage.h (storage),
                         controls.h (controls), device.h (device) — plain C++, no ordinals
  libeapp/               the iPod frameworks implementing those headers on the host; each entry
                         point records the ordinal the hardware knew it by (imports.json).
                         arm_abi.cpp adapts them to the ARM calling convention, and is compiled
                         only for the pure recompilation
  platform/sdl3/         window, audio, keyboard, the macOS menu (desktop)
  platform/null/         no I/O (headless runs)
  game/                  the game, every function decompiled by hand (no generated code)
    game_state.h         names for recovered globals and structures (one place, one name each)
    state.h, guest.h     the game's state blocks as packed C++ structures overlaid on guest
                         memory (`play_state()`, `as_object(addr)`, …), offsets pinned by static_assert
    libc.{h,cpp}         the ARM C library the game used (memory, strings, divides, heap, sprintf)
    dispatch.cpp         call_indirect: the 14 addresses that must still be addresses
    shims.h              the ARM-ABI entries (`f_<address>`) those land on; everything else
                         calls the C++ names directly
    fixed.h              16.16 fixed point: `to_fixed`/`to_whole`, and the `Fixed16` type whose
                         `*` cannot forget the shift a raw multiply needs
    cheats.{h,cpp}       this port's cheats, and the rule that a round played with one does not
                         count; cheats_menu.cpp is the screen, under Options
    round_history.{h,cpp}  the best score on each of the 54 holes and the ball's path on the round
                         that scored it — what the 0x144-byte save record had no room for
    host_text.{h,cpp}    labels for the rows this port adds, which no resource pack has a word for
    records.h            the records read from course/hole files and the loader's tables
    calling.h            what is left of the ARM ABI: dispatch-table entries and guest-stack scratch
analysis/                the reverse-engineering evidence: the function table, live coverage
                         from play sessions, the feasibility assessment
reference/               frozen copies of the sources this work was written against
                         (see reference/MANIFEST.md)
```

## Saved games

The game saves on its way out and reads it back at start-up: your name, the courses unlocked,
the best rounds, the statistics, and a round in progress. Where those bytes live is the
platform's choice — `src/platform/save_store.h` is a name-and-bytes interface, and a platform
returns its own implementation from `Platform::create_save_store` (a console's save archive, an
app's private storage, a browser's local storage). Returning nothing accepts the default: one
file per save in the game's own directory, `jdmgp.sav` and `jdmgp2.sav`, as the iPod did.

Nothing above that interface knows where a save went. `src/libeapp/async_file.cpp` routes both
directions through it, so a platform that keeps saves somewhere unusual reads them back from
there too.

## What this port adds

The iPod game is played exactly as it was; these sit beside it, and each of them is a file in the
same store the saved games use, so nothing here is written into `jdmgp.sav` — a save still means
to the original game precisely what it always did.

### Cheats

**Options ▸ Cheats** (`src/game/cheats.h`, and the screen in `cheats_menu.cpp`) holds five
toggles, saved in `cheats.txt`:

| | |
|---|---|
| **Unlock courses** | all three courses selectable, however few have been earned |
| **No stroke limit** | the ten-stroke limit raised to 99 |
| **No out of bounds** | a ball that leaves the green is put back where the stroke was played from; the stroke still counts |
| **Aim guide** | the aim line follows the power meter while it swings, instead of showing the full reach |
| **Ghost trail** | the path the ball took on your best round of this hole, drawn faintly while you line the next one up |

Two things are worth knowing about how they behave.

The first is that **a cheat is a choice, not a change to the saved game.** Unlocking the courses
does not write to the save record — it widens the gate the course carousel reads. Turn it off and
exactly the progress that was earned is back, and the file is untouched either way.

The second is that **a round played with a rule-changing cheat does not count**: no best round
under the course picture, no statistics, no course unlocked by it, and nothing added to the round
history below. The round is still played out to the end — the card and the closing message are
the same — it simply leaves no trace. Without that, one toggle would quietly rewrite every number
the game keeps and there would be no way to tell which of them were honest. It is deliberately
sticky: switching a cheat on for one awkward hole and off again before the card is written still
voids the round.

Ghost trail is the exception, and is marked as an aid rather than a cheat. It draws your own past
path on a hole you have already finished, so it can tell you nothing about a hole you have not,
and a round played with it still counts.

### The round history

The 2008 save record is 0x144 bytes with nothing spare in it: three numbers and one best round a
course. A good hole in a bad round left no trace at all. `src/game/round_history.h` keeps what it
had nowhere to put, in `rounds.txt` beside it —

* the best score on each of the 54 holes;
* how many rounds each course has seen, and what they average;
* the path the ball took on the round that scored each hole's best, which is what the ghost
  trail draws.

A path is stored in course coordinates — the same 0..255 grid the ball is bounded to — so a point
costs two bytes, and a hole's path is thinned to at most 128 points on the way in. All 54 come to
a few kilobytes. The Statistics page shows the rounds finished, how many holes have a best on
file, and each course's average, under the three numbers the iPod showed.

A hole only counts as finished when it is holed out. One that runs out its strokes is not a score
anyone set and not a line worth following, so neither its best nor its ghost is recorded.

## Controls

The iPod had one wheel and five buttons, so the game has seven things a player can do: scroll
left, scroll right, select, play/pause, menu, rewind, fast forward. `src/platform/
input_bindings.h` holds the table that says which input performs each, the defaults, the rule
that one input does exactly one thing, and the file the bindings are saved to. It is portable:
a binding is a plain `InputCode` that only the platform which produced it interprets.

On macOS, **Settings…** (⌘,) opens a window with three tabs — General, Input and Graphics; on
Windows, Ctrl+, opens the same three tabs as a Win32 window (`src/platform/sdl3/win32_settings.cpp`).
General holds the frame rate — 30, 60 or unlocked — and whether the window title shows the rate
the game is actually running at. The default is 30 on every build. The game's own clock advances
a sixtieth of a second a frame whatever the pace, so 60 is the speed its timers were written for
and 30 is that at half speed; the Switch, which paces on the display's flip, takes every second
one for 30. Graphics chooses how the picture is enlarged (below). All of them
are saved as `settings.txt` in the same store as the saved games and the key bindings, and read
back at start-up; `--fps=` is a deliberate instruction for one run and outranks the saved rate.
`src/platform/settings.h` is the portable half — the struct, the file, and the rule that a file
written against another meaning of these settings is not read — so a second platform's window has
something to edit and saves it the same way. The frame rate is the host's setting
rather than the window's, since the L key changes it too, so the window asks the host to change it
and is told what it became. Input has a row per action and a menu of keys to choose from, plus a Restore Defaults
button. Changes take effect at once and are written to the
platform's save store, so they survive a restart. Choosing from a list rather than asking the
player to press a key is deliberate: a "press any key" prompt has to intercept the keyboard, and
on macOS that means being right about the responder chain or an event monitor, where a list
cannot fail quietly.

Each action can have **three** inputs, because one name for a thing is often not enough: the menus are
a vertical list, so a player reaches for ↑ and ↓, while the wheel those stand for turned left and
right. The defaults give the wheel both pairs — ← / → and ↑ / ↓ — with Space to select, Escape for
menu, Tab for play/pause, and `[` and `]` for rewind and fast forward. The third is the gamepad's, so that plugging one in
costs the keyboard nothing. None of them is a letter,
which is deliberate: a bound key never types (see Typing below), so letters bound to controls would
be letters a name could not contain. Bindings are saved with the format number they were written
against, so a default that moves does not live on in files already saved. A bound key always
wins: the window's own shortcuts (F or F11 for
fullscreen, L to switch between the paced rate and unlocked) only get the keys nothing has been bound to, so anything
can be rebound to anything. P takes a screenshot and Q quits; those two are the program's rather
than the device's, and are not bindable. **Exit** on the game's own main menu ends the program too:
the row asks the firmware to put the eApp away, and on an iPod that means the iPod's menu comes
back, while here there is nothing to come back to. Escape used to quit as well and no longer does — it is the
Menu button now, which is what a player reaches for to back out of a screen.

One press of a scroll key is worth one row: the wheel counts 120 detents to a turn and a menu
moves a row every eight of them, so a press is worth eight. A press worth a single detent, which
is what this used to be, meant eight presses a letter. A scroll key **held** keeps the wheel
turning, as a thumb resting on it would: the press is a tap and nothing more for a quarter of a
second, so that a tap stays a tap, and then the wheel turns steadily — at the rate a stick pushed
all the way over turns it — until the key comes up. The keyboard's own auto-repeat used to stand
in for this, and it was the reason the aim lagged the keys: a repeat arrived a whole row at a
time at the system's rate, several times faster than the game polls the wheel, and each detent
was queued as a sample of its own for the game to read one a frame, so a long hold left a queue
of turns that kept the aim moving after the key had come up. The hold is paced by the frame now,
and a frame's worth of live turning reaches the game as one sample, the way a real wheel reports
its position: the aim moves in the frame the key does, and stops when it does.

### A gamepad

A gamepad is another set of inputs the same table names, and is bound in the same place
(`src/platform/sdl3/sdl3_gamepad.h`). Out of the box the D-pad turns the wheel a row at a time, A
selects, B is Menu, Start is Play/Pause and the shoulders are Rewind and Fast forward — every one
of them rebindable, and every one of them listed in Settings ▸ Input beside the keys.

The **left stick is the wheel**, and it is the reason to want a pad at all. The wheel counts 120
detents to a turn and eight to a menu row, which is finer than a key can express: a key press is
worth a whole row every time, while a stick held a little off centre is worth a detent now and
then. Deflection is squared before it is converted, so the first part of the throw is the slow
part — which is what lining a putt up wants — and a stick pushed all the way over is worth six
menu rows a second. The fraction of a detent left at the end of a frame is carried into the next
rather than dropped, so a barely-tilted stick turns the wheel slowly instead of not at all. The
stick is not bindable, because it is not a button; it is the wheel.

A mouse wheel and a trackpad do nothing. They were read as the click wheel once, and the reason
they no longer are is worth keeping: a swipe up a trackpad carries about a quarter of its travel on
the sideways axis, and the coast at the end of one arrives on that axis alone with no vertical part
left at all, so no test on the event — or even on the whole gesture — could reliably tell a
deliberate sideways swipe from an accidental one. A control nobody chose and nobody can rebind kept
turning the menu. Every control is now a key, and every key is in Settings ▸ Input.

`MINIGOLF_TRACE_INPUT=1` prints every key the window receives and what it is bound to. What a device actually sends
differs by device and by the system's own settings, so when a control misbehaves this is the
difference between reading the answer and guessing at it.

The window keeps the screen's 4:3 shape however it is dragged, so the picture always fills it.
**-** and **=** step it through whole multiples of 320×240, from 1× to 8×; F or F11 is fullscreen.

Another platform gets rebinding by supplying two things: its own default codes
(`set_default_bindings`) and a way to ask the player for one. Everything else already works.

### Typing

Spelling a name out on the wheel is what the iPod could do, and it still works everywhere. Where
the platform has a keyboard, the name can simply be typed instead. A platform opts in by
answering `true` from `Platform::text_input_supported()` and filling in `FrameInput::typed` —
the characters typed this frame, how many backspaces, and whether Return was pressed. `src/
platform/text_entry.h` carries that to the game, and `name_entry_typing()` in
`src/game/name_entry.cpp` applies it through exactly the steps the wheel's own handler uses: the
same store, the same length, the same sixteen-character limit, and Return finishing the name as
the tick glyph does. A character the game has no letter for is ignored, and lower case is folded
to upper, because the alphabet on the wheel is the authority on what a name may contain. SDL3 is
the platform that has it so far; the headless null platform does not.

### Scaling

The game draws 320×240 pixels and nothing else — the rasteriser is fixed at the iPod's resolution
(`src/libeapp/gles.h`), and the geometry is fixed-point maths tuned to that screen — so there is no
higher internal resolution to render at. What can be chosen is the last step, enlarging that
picture to a window many times its size, and Settings ▸ Graphics offers three:

- **Sharp** (the default) enlarges by a whole number with no filtering at all, into an intermediate
  texture, and then smooths *that* down to the window. The smooth pass only ever has a fraction of
  a pixel to deal with, so pixels stay square and the same size while edges stop crawling as the
  window is resized. It costs one extra texture and one extra pass a frame.
- **Nearest** is the old behaviour: hard blocks, and at any window size that is not a whole
  multiple of 320×240 some blocks come out a pixel wider than their neighbours.
- **Smooth** is plain bilinear — soft, and softer the larger the window.

**Whole multiples only** goes further: the picture is drawn at the largest whole multiple that
fits and the remainder of the window is border. Every pixel is then exactly the same size, at the
cost of not filling the window.

Sharp is the default because it is right at every window size: at a whole multiple it is
pixel-for-pixel identical to Nearest, and between multiples it is the only one of the three that
does not either distort pixels or blur the picture.


## Sound

The sound effects and the music both play through SDL audio streams, which is what lets the game's
own audio settings mean anything.

The effects are `.wav` and go to SDL as they are, one voice per sound, four at a time as the iPod
had. The music is AAC in `.m4a` — six tracks, 44.1 kHz stereo, 45 to 115 seconds — which SDL does
not decode. `src/platform/sdl3/music_decoder.h` is the seam that does: on macOS it is
AudioToolbox's `ExtAudioFile`, a system framework rather than a new dependency, decoding the track
as it plays and handing back PCM that goes into an SDL stream exactly as a sound effect's samples
do. Elsewhere there is no decoder yet, `music_decoding_supported()` answers false, and the game
plays on in silence after saying so once.

The music used to be a child `afplay` process. It played the file and offered nothing else: no
volume this program could set, no way to stop it that was not a signal, and macOS only. Every one
of the game's audio settings needs the audio to be ours, so it is:

* **Options ▸ Music: OFF** stops the track. Turning it off is one framework call (Audio #45) and
  nothing else, and that call used to only write itself to the log — the music played on whatever
  the setting said.
* **Options ▸ Sound FX: OFF** silences the effects, and #5 now stops one already sounding rather
  than letting it finish.
* **Volume** on the main menu sets the volume of the music and the effects together, as the
  iPod's own volume did — it belonged to the device, not to one part of the game. The level
  reaches the platform through `Platform::set_audio_level` and becomes the gain on every stream.
* **Options ▸ Brightness is gone.** It set the iPod's backlight, and nothing this port runs on
  has one it could set, so the row is taken out of the Options menu (`options_enter` in
  `src/game/menu.cpp`) and the pause menu's Options shows three rows rather than four. The
  oracles still see the original's menu: `--no-port-additions` and `--emulator-firmware` keep
  the row, and its page is still in `page.cpp`.

The Volume and Brightness sliders did not respond to the wheel for a long time, and the note
here blamed `input_gather` for filing a wheel direction in the wrong slot. It was not that:
`input_gather` matches the original instruction for instruction, and the two level functions in
`src/game/sounds.cpp` were reading each direction's step from the *other* direction's slot,
which is always zero. They read their own now, and `tests/scripts/volume.script` — the first
script to turn the wheel on the page — is compared against the pure recompilation
(`recomp_volume`), which agrees on every call and every frame.

`MINIGOLF_TRACE_AUDIO=1` prints what the audio is asked to do — the track opened and its shape,
the loops, the stops, the volume. Sound that does not come out has no other symptom and nothing
on screen to read, which is the same reason `MINIGOLF_TRACE_INPUT` exists.

## The Nintendo Switch

There is a homebrew build for HorizonOS — a `.nro` the Homebrew Menu can load.

    tools/switch-build.sh

The toolchain is devkitPro's devkitA64 with libnx. It is not installed on this machine and does
not need to be: the script builds the image `devkitpro/devkita64` plus `switch-zlib` and runs the
whole build in it against this working tree, so Docker is the only requirement. The result is
`build-switch/minigolf-switch.nro`.

To install it, copy two things to the SD card:

    minigolf-switch.nro   ->  sdmc:/switch/minigolf-switch.nro
    the game's 88888 folder -> sdmc:/switch/minigolf/88888/

The second is the game's own files, exactly as they sit on the iPod, and they are not included
here for the same reason they are not included anywhere else in this repository. If they are
missing the program says so on screen rather than failing silently.

**Controls.** ← / → or the left stick turn the wheel, A selects, B is Menu, X is play/pause,
L and R are rewind and fast forward. **Minus** opens the controls screen and **Plus** quits; those
two are the only buttons that cannot be rebound, since either would otherwise shut the door behind
the player. Everything else goes through the same portable binding table as the desktop's
keyboard, and the changes are saved to the SD card exactly as they are on a desktop.

The controls screen is text on the console rather than part of the game's picture — the console
and the game's framebuffer are the same display, so it takes the display for as long as it is up.
Up and Down choose a control, Left and Right its first or second button, A binds whatever button
is pressed next, Y clears one. Asking for a button press is safe here in a way it was not on
macOS: reading the pad is one call and cannot be intercepted by anything.

**What it does not do yet.** There is no music: the tracks are AAC in `.m4a`, and this console has
no decoder for them that is ours to use. There is no software keyboard, so a name is spelled out
on the wheel as it was on the iPod. Sound effects do play — `src/platform/switch/switch_audio.cpp`
is a small mixer over libnx's `audout`, since the console gives homebrew one PCM stream and no
mixer of its own. The .wav decoding it needs is portable and lives in `src/platform/wav.cpp`.

**It has never been run.** There is no Switch to hand, so what is claimed here is what can be
shown: it cross-compiles clean with warnings as errors, it links against libnx, the `.nro` is
well formed (icon and title checked by reading the file back), and the parts that are plain
logic — the memory model and the .wav decoder — are shared with the desktop build. The rest is
code review.

If the first run on real hardware goes wrong, this is where to look:

| What you see | Where it is |
|---|---|
| A message about the game's files | Working as intended: it says what is wrong with them |
| "Something went wrong…" and a message | A fatal error, printed rather than swallowed; the text names it |
| It closes straight back to the menu | The console was never reached — likely the loader, not this |
| Black screen, no sound | `present()` in switch_platform.cpp: the blit, or the framebuffer's format |
| The picture, but nothing responds | `poll()`: the pad, or the bindings file on the SD card |
| The picture and no sound | switch_audio.cpp: audout opened, or the .wav decoding |
| Out of memory at start-up | Launch it from a game (hold R) rather than the album: applet mode's heap is small |

The music is the one thing that would need a new dependency rather than a fix: devkitPro's portlibs
carry `switch-ffmpeg`, which would decode the AAC, and it is a large thing to add for one feature
on a platform nobody here can test.

**The picture** is magnified by exactly three (720 is 3 × 240) with black bars either side, which
is what "Whole multiples only" does on the desktop. The frame rate and scaling settings are not
offered here: the console's own vertical blank paces the game, and at a whole multiple every
scaling mode draws the same pixels.

**The memory model.** The guest's address space is 790 MB of mostly nothing — the gap between the
game's image at `0x18000000` and the iPod's IRAM at `0x40000000`. A desktop maps the lot in one go
and lets the pages arrive as they are touched; a console has neither the address space to spare nor
the paging to make it cheap, so it allocates the four regions the guest actually uses (about 28 MB
in total, measured against the longest recorded session and then given room). Both models are in
`src/runtime/memory.cpp`, and `cmake -DMINIGOLF_REGION_MEMORY=ON` builds the console's on a
desktop.

## Windows

There is a 64-bit Windows build — `minigolf.exe`, the same SDL3 program as the macOS one.

    tools/windows-build.sh

The toolchain is MinGW-w64. It is not installed on this machine and does not need to be: the
script builds an Ubuntu image with the cross-compiler, its zlib and SDL3's own mingw development
package, and runs the whole build in it against this working tree, so Docker is the only
requirement — exactly as for the Switch. The script is a wrapper around
`../common/tools/windows-build.sh`, which every title shares; the toolchain file beside it is
`mingw-w64.cmake`, and it
serves as well on any machine with the `x86_64-w64-mingw32` toolchain on its path; on Windows
itself `CMakeLists.txt` also spells every option the MSVC way, so `cmake -B build` there is the
other route. The result is `build-windows/dist/`: `minigolf.exe` beside the `SDL3.dll` it loads.
Nothing else is needed — the C++ runtime is linked in.

To run it, copy that folder to a Windows machine and start the .exe. On first launch it asks for
the game's zip with the native file browser, as on macOS, and puts the files in
`%APPDATA%\iPod Mini Golf\88888` beside the saves and settings (see "The game's files"). There is no console
window: the game is one window and nothing else. Started from a terminal it joins that terminal
and prints there as it always did, and anything fatal with no terminal to print to is a message
box, so a program that cannot start still says why
(`common/src/ipod/platform/windows_console.h`). The headless build is deliberately still
a console program, since its output is the whole point of it.

**Settings.** Ctrl+, opens the settings window — the same three tabs as the Mac's, General,
Input and Graphics, as a Win32 window of the game's own (`src/platform/sdl3/win32_settings.cpp`),
owned by the game's window so it stays in front of it. Tab moves between the controls and Escape
closes it. The two windows share everything but the widgets: the actions, the keys on offer, the
saved files and the hooks into the host are the portable code behind both, so a change in one is
a change in the other. Its controls take the modern look from the manifest in
`src/platform/sdl3/win32_settings.manifest`, which the build embeds.

**Music** plays, through Media Foundation: the tracks are AAC, which SDL does not decode, and
the decoder behind `src/platform/sdl3/music_decoder.h` is AudioToolbox on macOS and a Media
Foundation source reader on Windows — each the system's own, so there is nothing to ship. A
Windows "N" edition without the Media Feature Pack has no AAC decoder; the program says so once
on the console and plays on without music, as every other platform still does.

## Portability

The project targets Apple clang, MSVC, MinGW and the NDK, and one day a console toolchain.
Everything host-specific is either in `src/platform/` or behind a capability check with a
documented fallback: the guest address space (`reserve_span` in `src/runtime/memory.cpp`, three
implementations), backtraces on a watch hit, `localtime_r`, and the music decoder
(`src/platform/sdl3/music_decoder.h`), which is AudioToolbox on macOS, Media Foundation on
Windows, and degrades to silence with one warning where there is none yet. `CMakeLists.txt` carries an
MSVC spelling of every compiler option.

Two things a port must know. The game's structure overlays read guest words directly, so the
host must be little-endian — `src/game/guest.h` asserts it and says what a big-endian port would
have to change. And the guest address space is one ~790 MB reservation, committed lazily; a
target with neither demand paging nor that much memory needs `guest_pointer` to map pages on
first use instead, which is the note left at `reserve_span`.

## Licence

GPL-3.0-or-later, like the rest of the ports; the tools in `../common/tools/` are MIT. See
`../LICENSING.md` for the split and for what a licence here does not cover — the game itself is
not ours to license, and no part of it is in this repository or in any release.

## Releases

**The game's folder is `88888`.** That is the folder from the iPod that a player has to
supply; no artifact contains it, and nothing runs without it. Each built artifact carries a
readme that says so, names that folder, and gives the place it ends up
(`%APPDATA%\iPod Mini Golf\88888` on Windows, `~/Library/Application Support/iPod Mini Golf/88888`
on macOS).

    ../common/tools/release.sh <version> Mini Golf

builds every artifact this machine can and lays them out under `release/<version>/` with a
`SHA256SUMS` beside them:

    minigolf-<version>-macos.zip         "Mini Golf.app", universal, macOS 11 and later
    minigolf-<version>-windows-x64.zip   the .exe and SDL3.dll
    minigolf-<version>-switch.zip        the .nro, for the Homebrew Menu

The same script is what continuous integration runs (`.github/workflows/release.yml`), so a tag
and a laptop build the same thing. Run it with no title named and it does every title in the
tree. It signs nothing: an unsigned .app is quarantined by macOS the first time it is opened
from a download, and each artifact's readme says how to open it anyway (right-click ▸ Open, or
`xattr -dr com.apple.quarantine`). Signing needs a paid Apple developer account and is a
decision for whoever publishes.

The macOS artifact is built against SDL's own release framework rather than this machine's SDL,
which is what makes it universal and lets it run on macOS 11: a Homebrew SDL is built for the
machine it was installed on and would pin the app to that architecture and that macOS version.

The console build cannot ask for the game's files — it has no file browser — so on that one
they have to be at `sdmc:/switch/minigolf/88888/` before it starts. Each artifact's readme says
so in its own words.

## Building

Requirements: CMake ≥ 3.21, a C++17 compiler, Python 3.10+, SDL3 (via `pkg-config`), zlib, and
the game's folder (not included — it is the owner's game data; `tools/funcs.py --help` shows
where the tools expect it).

```sh
cmake -B build && cmake --build build
./build/minigolf          # on macOS: ./build/minigolf.app/Contents/MacOS/minigolf
```

The windowed build is a bundle on macOS, so it lands at `build/minigolf.app/Contents/MacOS/minigolf`
rather than `build/minigolf`; the headless build is a plain executable as before.

**This one needs no generator step.** `src/game/` is the whole game by hand, so `gen/` holds
only a function table and nothing generated is compiled into the build. The recompiler is still
used, but as the test oracle: `tests/check-recomp.sh` translates every function in the game's
binary, builds that as a second headless program, and compares it against this one call by call.
That needs your copy of the game the same way the tests do, and writes to `build/gen-pure`
rather than to `gen/`.


The default build is **Release**: `-O3 -g`, with its symbols and its assertions kept. That matters
more here than in most projects, because the renderer is a software rasteriser and the optimiser's
output *is* the frame rate — the same 29 200-frame run took 2.93 ms a frame at `-O1` and 2.40 at
`-O3`, before a line of the rasteriser was changed. `assert` is deliberately left live; it was
measured to cost nothing. `-DCMAKE_BUILD_TYPE=RelWithDebInfo` is the debugging configuration and is
`-O1 -g`, which is what makes a recompiled function steppable next to the decompiled one.

**Render scale.** Settings ▸ Graphics, or `--render-scale=N`, draws at N times 320×240 and hands
the platform the larger picture. The game is never told: it goes on computing in 320×240, and the
scale is applied in exactly one expression on the way to the raster, so nothing it draws moves.
What gets finer is where an *edge* lands — the arcs of the aiming ring, anything the course
transform has been through. A 1:1 sprite blit gains nothing and must gain nothing: it is still
recognised as 1:1 in the game's own pixels and still sampled nearest, so the art enlarges as the
same hard blocks. The frame is drawn on every core, in horizontal stripes, which is bit-for-bit
what one thread would have drawn — `--render-threads=1` pins it for measuring. On an M1 Max, over
3 000 frames of one hole: 2.41 ms a frame at 1×, 1.87 at 2× (four times the pixels, but past
the point where the work is shared out), 5.88 at 4×, 25.39 at 8×.

The main build contains no generated code. `tools/funcs.py` and `tools/emit.py` can still emit
the whole game with no hand-decompiled replacements into `build/gen-pure/` — the pure
recompilation, the original code translated instruction by instruction.

## The game's files

The game's folder (`88888`, 169 files, ~47 MB) is the player's own copy. On first launch the
game has none, so it opens the native file browser and asks for a zip of that folder
(`8888.zip`; the folder may sit at any depth inside it). Every file in the zip is checked
against `src/gamedata/manifest_data.cpp` — the size and CRC-32 of each file as shipped,
generated by `tools/manifest.py` — and only a zip whose files all match is unpacked. A wrong
or damaged zip is refused with the reason, and the browser opens again.

The files go to the per-user data directory (`src/platform/paths.h`), which is also where the
game's saves, settings and, later, mods live:

| macOS   | `~/Library/Application Support/iPod Mini Golf/88888` |
|---------|------------------------------------------------------|
| Windows | `%APPDATA%\iPod Mini Golf\88888`                     |
| Linux   | `$XDG_DATA_HOME/ipod-mini-golf/88888` (default `~/.local/share/…`) |

`MINIGOLF_DATA_DIR` overrides the location. The installed files are verified on every launch
(the saves the game writes there are ignored); if any has gone missing or changed, the file
browser comes back. `./build/minigolf --install-zip=8888.zip` installs without the dialog,
and `--gamedir=DIR` bypasses all of this and runs straight from a directory.

`cmake -B build -DMINIGOLF_SANITIZE=ON` builds with ASan/UBSan. `cmake --build build --target
format` runs clang-format over the hand-written sources.

## Working on the game code

Every function is decompiled; the remaining shape of the code comes from the machine underneath:

* Asking the platform for something is an ordinary C++ call into `src/framework/`:
  `gfx::draw_arrays(gfx::Primitive::Quads, 0, count)`, `audio::play_sound(sound)`,
  `storage::perform(request)`, `device::battery_level()`. Values that name a thing the hardware
  enumerated are `enum class`, so a texture target cannot be passed as a primitive. Game
  functions take and return ordinary values; a `Cpu&` appears only in the ARM-ABI shims
  (`f_XXXXXXXX`) and `dispatch.cpp`, where the ABI is the point. Nothing carries the original's
  return addresses around.
* Locals a framework reads or writes (an out-parameter, a string for the text renderer) are
  reserved on the guest stack with `GuestScratch` and addressed with `frame.at(offset)`; those
  are the only stack frames left.
* A function that works on one of the game's records takes a reference to it — `ImageRecord&`,
  `FontRecord&`, `PackRecord&` — not the address it lives at. `as_image(address)` and its
  siblings are the bridge from a stored address to the record, and `address_of(record)` the way
  back for the fields and framework arguments that still hold one.
* Geometry is 16.16 fixed point. `to_fixed(30)` and `to_whole(x)` are the conversions, and
  `Fixed16` (in `fixed.h`) is the type to do arithmetic in — its `*` keeps the scale, which a
  raw 32-bit multiply silently loses.
* Game state is reached through the structures in `state.h`. Most of them overlay the guest
  address space, and that is not laziness: the frameworks are handed those addresses (the text
  block alone is handed out hundreds of thousands of times a session), the file framework
  reads and writes the settings block, and `INPUT_STATE` is initialised data that comes out of
  the player's own copy of the game. What nothing outside reads has moved to the host: the
  random generator, the tracked-allocation registry, the sound slots, the wheel/button slots,
  and the menu item tables. Before moving a block, check it: no address inside it may ever be
  handed to a framework.
* Records read out of course and hole files, and the tables the loader builds, are the
  structures in `records.h`. The only raw `ld`/`st` left are the string and C-library
  primitives (`strings.cpp`, `libc.cpp`), which are the memory primitives themselves.
* Screens are typed host function pointers (`Screen` in `screens.h`), not addresses.
  `src/game/dispatch.cpp` is down to the 14 addresses that must stay addresses: the image's
  three entry vectors and the completions the framework itself dispatches (`src/libeapp/
  async_file.cpp` reads five of them out of the guest request record at +0x34). A new one needs
  an entry there — `call_indirect` is fatal on an unknown address.

## Debugging aids

* `--trace-entry=ADDR[,ADDR]` prints the registers every time a recompiled function at that
  address is entered.
* `--call-log=-` streams the framework-call log to stdout.
* The `shot` script action (or P in the window) writes `build/shot-NN.ppm` and prints a hash;
  `tools/ppm2png.py` converts to PNG.

## Status

Every one of the game's 333 functions is hand-decompiled, and the SDL3 build runs the game in a
window.

What the code looks like now: the platform is reached through plain C++ interfaces with
`enum class` arguments, not framework ordinals; no game function takes a `Cpu&`; records are
passed as references rather than addresses; geometry converts through `to_fixed`/`to_whole` and
`Fixed16` rather than bare shifts; and every quantity that describes the game — the screen, the
three courses, the eighteen holes, the game modes — is named once, in `game_state.h`.

What is left of the machine underneath is deliberate and documented: the game's state still lives
in guest memory because the frameworks are handed pointers into it, fourteen dispatch entries are
still addresses because the framework dispatches them by address, and 23 ARM-ABI shims stand
where those two meet. `PLAN.md` records the schedule, the progress log and the open questions.
