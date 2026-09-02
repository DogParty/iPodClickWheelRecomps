# The shared core

Code that belongs to no single title, used by every recomp under `recomps/` by being **compiled
and imported from here** rather than copied into each tree.

That is the whole point of this directory, and it exists because the copies rotted. Until now a
title started as a copy of the one before it, recorded in its `reference/PORTED.md` with the
SHA-256 of each file at the moment of the port. It worked, and it drifted: by the time there were
two titles, `src/libeapp/gles.cpp` was 800 lines in one tree and 1707 in the other. Features drift
harmlessly — but *fixes* drift too, and that is the part that hurt. A shutdown crash was found and
fixed in the Lost recomp on 2026-08-26 and was still live in Mini Golf on the 27th, where it
crashed the game every time music had played; the one-line rule that keeps a 1:1 sprite blit sharp
had the same history. Nobody was at fault. Copies simply do not carry a fix forward, and nothing
in the process was ever going to notice.

**Starting a new title?** `../NEW-RECOMP.md` is the playbook: the plan the four titles were
made with, genericised, from the measurements to the last green test.

## What is here, and what is not

A title keeps everything that is *about that title*: its `src/game/`, its `gen/`, its `analysis/`,
its recorded oracles under `tests/expected/`, its `imports.json` and the ordinal table that goes
with it, and every constant measured from its own binary.

What comes here is what turned out to be the same file twice. That was measured rather than
assumed — the two trees were compared after normalising their namespace and macro names, and the
result is why this directory exists at all:

| already the same file | 93–100% |
|---|---|
| `tools/recomp/` — the ARM-to-C++ recompiler | `image.py`, `cfg.py`, `cpp.py` identical; `arm.py` 93% |
| the guest runtime | `runtime.{h,cpp}`, `memory.h` identical; `cpu.h` 94% |
| the platform's portable half | `save_store`, `text_entry` identical; `input_bindings.cpp` 97%, `settings.cpp` 95% |
| parts of `libeapp` | `misc.cpp`, `heap.cpp` identical; `host_state.cpp` 98%, `arm_abi.cpp` 95% |
| the framework interfaces | `types.h`, `controls.h`, `storage.h` identical; `device.h` 99%, `audio.h` 97% |

Three files are genuinely divergent and are *not* here yet: `libeapp/gles.cpp` (69% alike),
`platform/sdl3/sdl3_platform.cpp` (59%) and `runtime/main.cpp` (58%).

### The third title's move (2026-08-27)

Texas Hold'em started with this directory in place and moved what it could *before* depending on
it (its PLAN.md, block 0b): `runtime/cpu.h` (Lost's, the superset that models the flag-field
`mrs`/`msr`), `runtime/runtime.{h,cpp}`, `libeapp/heap.{h,cpp}` and `gamedata/zip.{h,cpp}`. Each
left a forwarding header in all three trees; `runtime.h` keeps one thing per title — the
declaration of `game::call_indirect`, which each title's dispatch table defines. Mini Golf and
Lost were rebuilt and their suites run against the result (30/30 and 17/17).

One thing was tried and undone the same hour, and the reason is worth keeping. The four
framework headers `controls.h`, `device.h`, `storage.h` and `audio.h` are identical across the
titles too — but they *declare* functions each title's `libeapp` implements, and a declaration
that lives in `ipod::storage` forces every implementation into `ipod::storage` as well, in
three trees at once. That is the rule stated above — an interface and its implementation cross
together — and those implementations are exactly the per-title models (file, input, audio)
that still differ. They stay with the titles until the models are parameters.

The rasteriser grew seven entry points for Hold'em, all firmware behaviour rather than one
title's: `disable` (#35), `gen_textures` (#45 — names from 1, as the driver's counter), the
vector form of the constant colour (#148, `set_constant_color_vector`), the `mat4` helpers
`matrix_translate` (#169), `matrix_scale` (#171) and `matrix_rotate` (#173), and
`texture_sub_image` (#105, decoded through the whole-texture upload into a scratch name and
blitted, as the emulator does). Each is what the emulator's stub does (`reference/eapp-loader/
lib.rs`, `GlGenTextures`, `GlUniform4x`, `GlMatrixOp`, `upload_sub`). Two of them — `#171` and
`#105` — were found by the *picture* oracle: an ordinal a title calls and nobody implements
still logs its call, so the call-log oracle passes it by construction. The check that catches
it is `python3 -c` over a recording against `imports.json` for reached-but-unnamed ordinals;
Hold'em's `analysis/ordinals.txt` is that list for its recordings.

One rendering rule moved under `--emulator-graphics` with it: the box filter for minified
textures (`MINIFIED_RATIO`) is a deliberate improvement on the emulator's nearest sampling, and
the picture oracle — which runs with that flag precisely to draw as the emulator does — now gets
nearest sampling too. Hold'em draws its card backs at half size; the filter alone moved 2.5% of
that frame, all of it on the cards' edges.

### The fourth title (2026-08-27/28)

The Sims Bowling started, like Hold'em, with this directory in place, and its plan made a rule
of what the third title had done by instinct: **a shared-core change is verified in every title
before it is used** — Mini Golf's `tests/check-recomp.sh` still byte-identical, Lost's and
Hold'em's `gen/` re-emitted byte-identical and their suites green. Four changes went in under
that rule:

* **`tools/recomp/cfg.py`, two idioms.** armcc's 64-bit divide steps into an unrolled loop with
  `add pc, pc, rN, lsl #2` bounded by an `and rN, rM, #7` rather than a `cmp`/`addls` guard, and
  a second table in the same routine uses `lsl #3` — two instructions per case. `JumpTable` now
  carries a stride, and an *unconditional* computed jump is bounded by walking back to the mask
  that computed its index and forward again through the arithmetic that scaled it
  (`_mask_bound`; anything it does not recognise is still the old error). That was the only
  unwalkable instruction in a 71 000-instruction image.
* **The link scan runs to the nearest lr writer.** Hold'em found a return address prepared
  four instructions before its `bx`; Sims Bowling prepares one *twelve* before (`add lr, pc,
  #0x2c` at 0x18044a84) and then *twenty-four* before (`add lr, pc, #0x5c` at 0x18028f68, with
  a whole 16.16 multiply chain in between). Each time the window was one short the jump read as
  a tail call, the code after it — that path's epilogue — was never emitted, and the function
  returned a frame early. `_follows_link_setup` no longer has a window in any practical sense:
  it scans back until something else writes lr, and the equality test on the prepared address
  is the only guard. Lost and Hold'em re-emit byte-identical.
* **`set_render_scale` keeps the picture.** A resize used to repaint the buffer magenta, which
  assumed a game that redraws its whole screen every frame. Sims Bowling clears once at boot and
  redraws only what animates, so a resize stayed magenta until the next screen change. The old
  picture is now resampled into the new size (nearest) and the game sharpens what it redraws.
* **The generated `call_indirect` names its caller.** An unknown target used to be fatal with the
  address alone; it now reports the guest return address, SP and r0–r5, which is what a reader
  needs to find the site (`tools/recomp/generate.py`).
* **`libeapp/gles.cpp`: `glDrawElements` (#38).** Four of the five draws in a Sims Bowling menu
  frame are indexed and no title had implemented the ordinal. The rasteriser's vertex fetch is
  now `rasterise_indexed(mode, indices)`; `draw_arrays` names `first .. first+count` and
  `draw_elements` reads a byte, short or int index array (shorts for an unknown type, as the
  emulator's `draw_elements` reads it). The picture oracle at the menu is pixel-identical to
  the emulator's, which is the only oracle an ordinal nobody else calls has.

### A recompiler fix, from the third title (2026-08-27)

`tools/recomp/cfg.py` decides whether a register jump (`bx rN`, `mov pc, rN`) is a call or a
tail jump by looking back for the `mov lr, pc` / `add lr, pc, #N` that prepared a return
address equal to the instruction after the jump. The window was three instructions. Hold'em's
compiler put the `add lr, pc, #0xc` four instructions before its `bx r2` (`0x1800b8e8`), so that
`bx` was emitted as a tail jump and the code after it — the path's own six-register epilogue —
was never emitted; the function returned with 24 bytes still on the stack and the game read
address 9 as a pointer seven frames later. `LINK_SETUP_WINDOW` is 8 now and the scan stops at
an intervening `bl`. Re-emitting Lost with the wider window added the same kind of dropped
continuation to two of its functions; its oracles had never reached those paths. All three
titles' oracles are green on the new emitter.

## How a title reaches it

Shared code is namespace `ipod` and its headers are included with an `ipod/` prefix, so nothing
collides with the copy a title used to have. Each title then keeps a **forwarding header** at the
old path, which does nothing but include the shared one and pull its names into the title's own
namespace:

```cpp
// Lost/src/platform/save_store.h
#include "ipod/platform/save_store.h"
namespace lost::platform {
using ::ipod::platform::SaveStore;
using ::ipod::platform::save_store;
…
}
```

The point of that shape is that **no call site changes**. `lost::platform::save_store()` still
resolves, the include path a file already writes still works, and the migration can happen a file
at a time rather than as one rename of both trees.

It is `using` declarations rather than a namespace alias on purpose. An alias would make
`lost::platform` *be* `ipod::platform`, and then nothing could be declared into it any more —
but `platform` still holds things that are genuinely one title's, like its list of input actions.
`using` lets the shared and the title-specific sit under the same qualified name, which is what
makes it possible to move half a namespace and leave the rest.

`cmake -B build` in either title builds `ipod_core` from `../common` as a subdirectory. It carries
no opinion about warnings or optimisation: each title links it through the same interface target it
holds its own hand-written code to, so the shared sources are compiled under that title's rules.

## What shared code may not assume

Everything a title differs in has to become an argument, and each one is a place where two
binaries were measured and disagreed. So far:

* **The C++ namespace.** `Generator(..., namespace="lost")`.
* **Which functions are runtime rather than game.** `build_function_table(..., runtime_entries=…)`
  takes a set. The two titles decide it differently and both are right about their own image: one
  lists them in `src/runtime/arm_runtime.json`, the other has a contiguous address range, because
  armcc laid its soft-float library out differently.

The rule for adding to this directory: if a difference between titles is a *measured fact about a
binary*, it stays with that title and becomes a parameter here. If it is a difference only because
one tree was fixed and the other was not, it does not belong to either — it belongs here.

### The rasteriser, and what it cost to share

The iPod's GL ES driver was one piece of firmware and every title called the same one, so there is
now one reimplementation of it. Lost's was the more complete of the two — its file had every
function Mini Golf's had and twenty it did not, and Mini Golf's had none of its own — so Lost's is
the one that moved, and Mini Golf's was deleted.

Sharing it found **two places where the two games drive the driver differently**, and both were
missed by every call-log oracle in either project, because a call log records a buffer's address
and never its contents. They were found by rendering the same scripts through both and comparing
the pictures.

* **How a draw's attributes are recognised.** A game that re-points every attribute before every
  draw has told the driver which attributes that draw reads; the enable flags say only that
  something once used one. Lost does that. Mini Golf points once and draws many times, so under
  Lost's reading every draw after the first read as untextured. The enable flag is the
  conservative default and `gfx::set_attributes_repointed_per_draw` is how a title claims the
  other one. *(Worth noting: the justification originally given for Lost's rule — that it never
  imports `glDisableVertexAttribArray` — is true of both games and was never the operative
  premise. The premise is the re-pointing.)*
* **What an untextured draw is painted in.** Lost puts a colour in the constant register and its
  bars and panels come from it. Mini Golf never sets that register — it does not import the
  ordinal — and carries a flat draw's colour in the vertex array, as GL does by default. Painting
  from an unset register gave opaque white and whole screens came out blank. This one needs no
  title to declare anything: the state records whether the game ever wrote the register.

### The clock and the battery (2026-08-28)

`src/ipod/platform/device.{h,cpp}` — what the host machine can say about itself that the iPod's
firmware said about the device. It is here because it was in five places, and all five were wrong
in the same two ways; the Cubis 2 recomp found it by looking at a status bar that read 12:00 PM at
every hour of the day.

* **`miscTBD #12` returned nothing.** The games test the answer: a zero means "no clock" and they
  draw a hard-coded time instead (Cubis 2 at `0x1800de5c`, which then keeps the 12, :00 and PM it
  loaded four instructions earlier).
* **The hour was folded to 12 before the game saw it.** The games do that themselves, from a
  24-hour value — the same routine tests the hour against 12 and subtracts — so every afternoon
  read as a morning.
* **The battery was `return 100;`.** In all five.

It is the first file here with a platform split, and it has to be: a battery has no portable
spelling. IOKit's power-source API on macOS (the only framework link in `ipod_core`),
`GetSystemPowerStatus` on Windows, `/sys/class/power_supply/BAT*/capacity` elsewhere, and *full*
where the host has none — a desktop is a device that is always on the charger, and 0 would put
every game into its low-battery behaviour.

Each title keeps `set_emulator_device`, which puts the emulator's wrong answers back for
`--emulator-firmware`: every recording in every `tests/expected/` was made against them.

### The glyph reconstruction, and a model that fits fewer fonts than it assumed (2026-08-29)

`Filter::Glyph` takes a run of text's filtered coverage back to full contrast, so that a glyph
drawn 1:1 with a game pixel keeps a one-pixel edge at a render scale above 1. Cubis 2 found two
things wrong with it, and the second was live in every title that used the setting.

* **Its gate asked the wrong question.** `is_text_run` wanted an opaque texel in every cell, which
  a face small enough to be antialiased into permanent translucency never has —
  `fonts/maiandra-7.raw` holds two above 250 in the whole sheet. So the feature quietly declined
  exactly the fonts that most needed it. The run's ink is now the largest of its cells' own peaks
  and coverage is measured against that; at 255 the arithmetic is unchanged, so nothing that
  worked before moves.
* **Its curve erased ornate faces.** The contrast curve assumes a texel under half coverage is
  outside the letter and antialiasing put it there. Measured cell by cell, only 27 % of
  `rockart-11`'s ink and 15 % of `maiandra-7`'s is at full weight: for both, most of the letter
  *is* ramp and the ramp is the design. Cubis 2's HUD read `SCVRK` for `SCORE` at every render
  scale. The reconstruction is now bounded (`GLYPH_MAX_CONTRAST`) and, decisively, **floored at
  the faithful nearest sample** — a feature that is off by default must never render a glyph with
  less ink than leaving it off would.

The floor is also the answer to why gentler curves did not help: `Filter::Glyph` samples
bilinearly where a 1:1 blit otherwise takes `Nearest`, so it begins thinner than the faithful
value before any sharpening applies.

### The music decoder, which went the other way

Nothing says the newer half of a pair is always the same tree's. The rasteriser came from Lost;
the **music decoder came from Mini Golf**, which had already replaced a child `afplay` process —
macOS only, no volume this program could set, no way to stop it but a signal — with a real decoder
feeding an SDL audio stream like the sound effects. Lost was still spawning `afplay` and its own
source carried a TODO asking for exactly this.

`ipod/platform/sdl3/music_decoder.{h,cpp}` is that decoder, and both titles now use it. It is not
in `ipod_core`: a title finds SDL *after* it adds this directory, and the headless oracle build has
no SDL at all, so the file is published as `IPOD_CORE_SDL3_SOURCES` and each title compiles it into
its own window build. Lost gained volume control and a real stop along with it.

Everything else about the two rasterisers was one being ahead of the other. After the two rules
above, Mini Golf's frames differ from its old renderer by a mean of 0.22 of a level out of 255 —
the alpha-weighted texture filtering and the sprite-sheet edge clamping that Lost had and it did
not, which are corrections.
