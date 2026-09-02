# Do the games load?

The question this project was started to answer, first asked in `research/01`, never once tested.
RetailOS reaches its main menu ([research/10](10-the-resource-image.md) Addendum 30), the menu has an
**Extras** row, and Extras is where the games are — both the 56 purchased titles on the disk and
the built-ins RetailOS carries in its own image (research/01 Q13: `Brick`, `Parachute`,
`Music Quiz` and `Solitaire` are linked into OSOS, not separate eApps). Nobody had walked down
there.

Two things were needed and neither existed: a way to **see** the screen at every step rather than
only at the end of a run, and a **calibrated wheel** — some way to land the highlight on a named row
rather than on whichever row 16 clicks happened to reach.

This file is both: §1 is the instrument, §2 onward is what it measured.

**The answer is yes.** Boot → Language → main menu → Extras → Games → `Brick`, thirteen scripted
wheel gestures, and Apple's own Brick draws its playfield and moves its paddle when the wheel turns.
A purchased title gets as far as its own cover art and is then refused by Apple's DRM with
*"This game cannot be played."* Both outcomes are one command each, below.

**And since 2026-08-14 the answer is larger than that: Brick is *played*.** The centre button serves
the ball, a scripted paddle returns it six times, eight bricks come out of the wall and the score
moves seven times. §10 is that work — the serve, the ball's physics, the paddle's 24-pixel quantum,
and the four predictions that measured out to nothing on the way.

---

## 1. `tools/ipod-film` — the panel, recorded

`--bcm-dump=ADDR:W:H:FILE` writes the co-processor's surface once, when the run stops. Every screen
the machine passed through on the way is gone by then. `--bcm-film=ADDR:W:H:EVERY:DIR` samples the
same surface with the same converter on an instruction cadence, keeps every frame that differs from
the one before it, and writes a manifest saying when each appeared and how long it held.

Full documentation is in [`tools/ipod-film/README.md`](../tools/ipod-film/README.md). The three
things that matter for reading the rest of this file:

**The frames are the panel, not a picture of the panel.** A frame is 76 800 halfwords read out of
`Bcm::mem` at internal `0x000e0000`, expanded RGB565 → RGB888 by the identical expression
`--bcm-dump` uses. Exactly 320x240, no scaling, no interpolation, no cursor, no window chrome.
Measured rather than asserted: a film frame decoded by ffmpeg's PNG decoder and a `--bcm-dump` PPM
of the same instant **written by a different process in a different run** are byte-identical across
all 230 415 bytes.

**Filming does not perturb the machine.** Two arms, one flag apart, `BUDGET=4000000000
retail-boot.sh --clock=5 --stop-when-idle=400000000 --bcm-registry`:

| | unfilmed | filmed |
|---|---|---|
| Idle after | 1 812 316 856 | **1 812 316 856** |
| code buckets | 38 476 | **38 476** |
| ata commands | 706 | **706** |
| bcm | 4 kicked, 2 frame updates | **4 kicked, 2 frame updates** |
| `--bcm-dump` | 75 267 non-black | **`cmp`: the two PPMs are identical** |

That is by construction, not luck: `Machine::run(n)` runs `n` iterations of its loop and returns
having consumed exactly `n`, so issuing a budget in chunks issues the same iterations in the same
order. The film adds no instruction and skips none; sampling happens between chunks. The Idle count
also reproduces Addendum 30 §7's "post-fix, registry on, `GPIOL=8`" row to the instruction.

**A whole retail boot is five frames.** Deduplication is the difference between a readable artifact
and 900 identical PNGs:

```
film 0x000e0000 320x240 every 2000000 -> _out/film/armB
  908 samples, 5 frames, 5 distinct pictures
     0  @0             held       4000000        0 non-black   dark panel
     1  @3999970       held       4000000        5 non-black   the bootloader's first five pixels
     2  @7999970       held      44000000     2922 non-black   the handoff frame
     3  @51999952      held     950000000    76800 non-black   blank white — 52 % of the whole run
     4  @1001999952    held     814000000    75267 non-black   the Language list
```

**The instrument's one silent failure**, and it bit this investigation: a screen that appears and
disappears entirely *between* two samples is not in the film, and nothing in the output says so.
`Videos` is missing from §2.1's table for exactly that reason — it was on the panel for under one
2 M sample — and its absence made a five-row scroll read as a four-row one. Halve `--every` and see
whether a new frame appears.

---

## 2. Navigating RetailOS with a scripted wheel

Every run below is the same recipe with a different `--wheel` script:

```
tools/ipod-film/film.sh --out=_out/film/NAME --every=2M -- --clickwheel --wheel='…'
    # = retail-boot.sh --clock=5 --stop-when-idle=400000000 --bcm-registry --bcm-film=…
    #   BUDGET=4000000000
```

The boot is identical in all of them up to the first wheel event: dark panel · the bootloader's
five pixels · its 2 922-pixel handoff frame at 8 M · **blank white from 52 M to 1 002 M** · the
Language list from 1 002 M. English is the initial selection and nothing rotates it, so every screen
after this point is in English.

### 2.1 The main menu, and 16 clicks is not a row

`@1500M:touch,+2M:press=select,+30M:rotate=+16` ×5`,+30M:release` — five bursts of 16 clicks
delivered 20 000 instructions apart, one continuous finger-down:

| frame | @ | what |
|---|---|---|
| 5 | 1 543 999 952 | **the main menu** — `iPod`, Music / Photos / Videos / Extras / Settings / Shuffle Songs, Music selected |
| 6 | 1 549 999 952 | Photos |
| 7 | 1 563 999 952 | **Extras** |
| 8 | 1 593 999 952 | Settings |
| 9 | 1 599 999 952 | Shuffle Songs, and nothing moves for the remaining 488 M |

87 wheel frames posted, **0 dropped unread**, 87 `DATA` reads every one of which found a frame
waiting — so the firmware saw every click. And yet the five bursts did not produce five single-row
steps: by 1 600 M the highlight is at the bottom of the list, **23 M instructions before the fourth
burst even starts**, and the last two bursts (32 clicks) move nothing because the list does not
wrap. Three bursts walked five rows.

Note also that the menu does not appear until 1 544 M — **42 M instructions after the Select at
1 502 M** — so the first burst at 1 532 M was delivered into a screen that had not been drawn yet.

`Videos` never appears in the film. It was on screen for less than one 2 M sample.

### 2.2 The correction: it is the gesture, not the click count

Addendum 30 §9 measured *"sixteen wheel clicks moved the language list one row"*. §2.1 above shows
16 clicks moving one row, then three rows, on the same list in the same run. Both are true, and
neither is the ratio, because **the number of rows a burst moves depends on how the burst arrives**,
not only on how many clicks it contains.

The clean measurement isolates one gesture at a time. `@1500M:touch,+2M:press=select,+5M:release`
to reach the menu with the finger **off** the wheel, then four complete touch/rotate/release
gestures 60 M apart, of 1, 2, 4 and 8 clicks, filmed every 1 M:

| gesture | clicks | result |
|---|---|---|
| 1 | 1 | nothing |
| 2 | 2 | nothing |
| 3 | 4 | nothing |
| 4 | **8** | **exactly one row** — Music → Photos, frame 8 @1 770 999 952 |

30 frames posted, 0 dropped. So a discrete gesture needs **eight clicks to move one row**, and one
to four clicks move nothing at all — while a *continuous* 16-click burst with the finger already
down can move three. The natural reading is Apple's scroll acceleration, which is a real feature of
this UI, but nothing here isolates the mechanism; what is measured is the two behaviours.

The practical consequence is the one that matters for the rest of this file:

> **`touch, rotate=+8, release` is one row down. A long burst is not N/8 rows.**

### 2.3 Two frames, same pixel count, different picture

Frames 6 and 7 of the gesture run are both 75 791 non-black and both show the main menu with Music
selected — and they have different digests, 1 M apart. Whatever changed is a handful of pixels
somewhere in the chrome. It is a small live example of the rule this project keeps re-learning:
**the non-black count does not identify a screen.** The film separates them because it digests every
halfword; a `--bcm-dump` count would have called them the same frame.

### 2.4 Extras opens, and Games is one row inside it

Two independent runs reach it, by the two different primitives, and agree on the screen:

- **continuous**: `@1500M:touch,+2M:press=select,+40M:rotate=+16,+40M:press=select` with
  `--wheel-click-instr=200000` — the 16-click burst lands on Extras, and Select opens it.
- **discrete**: `…press=select,+5M:release` then three `touch,rotate=+8,release` gestures 60 M
  apart, then Select. Frames 7, 8, 9 are Photos, Videos, Extras — **one row per gesture, three for
  three** — and frame 10 @1 775 999 952 is the submenu.

Both land on a screen with 75 565 non-black pixels, and the picture is:

```
                    Extras
        > Clock            <- selected
          Games
          Contacts
          Calendar
          Notes
          Stopwatch
          Screen Lock
```

Seven rows, no scrollbar, the whole list on one screen. **Games is row 1** — one gesture below the
entry row. Two further 16-click bursts in the continuous run walk the highlight down to
`Screen Lock` and stop there, which is the same no-wrap behaviour the main menu showed.

Seven entries is the **whole** list, not a window onto a longer one: there is no scrollbar, blank
space follows `Screen Lock`, and the highlight refuses to go past it. So `Alarms` and `Voice Memos`,
which some descriptions of a 5.5G's Extras include, are not top-level rows in this build — the
`Alarm Clock` / `Alarm Time` / `Recordings` strings that are in the image sit somewhere else.

---

## 3. The Games list renders

`@1500M:touch,+2M:press=select,+5M:release` then, 60 M apart, three one-row gestures · Select ·
one one-row gesture · Select. Every step lands where it was aimed:

| frame | @ | screen |
|---|---|---|
| 5–6 | 1 544 / 1 546 M | main menu, Music |
| 7 | 1 574 M | Photos |
| 8 | 1 640 M | Videos |
| 9 | 1 708 M | Extras |
| 10 | 1 776 M | **Extras opened** — Clock selected |
| 11 | 1 846 M | Games |
| 12 | 1 910 M | a transitional frame |
| 13 | 1 912 M | **the Games list** |

Six gestures, six intended outcomes, no correction. And the Games list is built **2 M instructions
after the Select** — a 56-entry list off a FAT volume, and it is up before the next film sample.

The list is one alphabetical sequence with no separator between Apple's built-ins and the purchased
titles: `Asphalt4 · Bejeweled · Block Breaker Deluxe · Bomberman · Brain Challenge · Brick ·
Bubble Bash · Cake Mania 3 · Chalkboard Sports Baseball` fills the screen, with `Brick` — one of
Apple's four built-ins — sitting at row 5 between two bought ones. 74 160 non-black pixels, denser
than any menu above it because nine rows of text is more ink than seven.

So the answer to *"does the games list render"* is **yes**, and it renders every title the disk
carries.

> ⚠️ **Correction (2026-08-14): the disk carries 56 titles, not 196.** `iPod_Control/Games_RO` has
> **56 subdirectories**, counted from the FAT32 directory entries, which matches research/07's own
> heading *"The full archive is on the device: 56 games + keys"* and the 56 `Manifest.plist` files
> under `resources/games/purchased/`. The 196 in this section and in
> `tools/ipod-boot/DISK-IMAGES.md` had no source; both are corrected. Nothing else in this file
> depends on the number — the list renders, the navigation lands, and the eight-click primitive
> holds on a 56-row list exactly as recorded.

## 4. Launching a purchased title: the cover art draws, then the DRM refuses

Same descent, one more Select on row 0 — `Asphalt4`, a purchased eApp. Seven more frames:

| frame | @ | held | screen |
|---|---|---|---|
| 14 | 2 036 M | 2 M | the Games list, one detail changed — the Select was taken |
| 15 · 16 | 2 038 · 2 040 M | 2 M each | **the game's own cover art** appearing, under an `Asphalt4` title bar |
| 17 | 2 042 M | 82 M | the art, settled |
| 18 | 2 124 M | 6 M | the same art, a few pixels different |
| 19 | 2 130 M | 2 M | transitional |
| 20 | 2 132 M | 404 M, to the end of the run | **`Error` — "This game cannot be played. Connect your iPod to iTunes and reinstall the game."** |

That is a **nameable failure point**, and it is the one this project predicted: the launcher works,
the game's directory is found, its artwork is read off the disk and composited to the panel — and
then Apple's DRM check rejects the binary and RetailOS draws its own error dialog. 92 M instructions
(~1.3 s of PP5021C time) of splash screen, then the refusal.

Nothing about the *emulation* failed here. The disk's purchased titles are the copies this project
has documented since research/08: they are bound to hardware this machine is not.

> ✅ **Followed up 2026-08-14 — the DRM research *(not published)* §"What the DRM binds to, measured".**
> The gap between frames 18 and 20 is one function, `FUN_00131874` at `0x00131874`, entered exactly
> once at instruction 2 041 669 893. Inside it the manifest's PKCS#7 signature **verifies**, every
> file in the manifest **verifies**, the executable and its `.sinf` are **read**, and then the
> content-key unwrap returns non-zero and the dialog is drawn. The identity it binds to is the
> 8-byte **FireWire GUID** in the `sysinfo_t` block — read from the NOR, never from the disk's
> `SysInfo` — and the keybag's own `iEKInfo` names a **third** GUID that is neither the flash's nor
> the disk's. Editing the NOR so the machine presents the disk's identity changes 47 bytes of SDRAM
> and produces a digest-identical 21-frame film ending in the same refusal.

**`--enterlog=0x0024e808` (`eAppMotor`, the RTXC task RetailOS names in its own image) printed
nothing at all** — and *nothing at all* is what `trace` printed for a watched address that is never
reached, which is indistinguishable from having forgotten the flag. That is fixed in this commit: an
armed-and-never-reached watch now prints `--- arrivals at watched addresses: 0 ---` and names each
address. Zero is a result and it has to look like one.

## 5. Launching a built-in: Brick runs

`Brick` is row 5 of the Games list, and it is the right target: research/01 Q13 established that the
built-ins are **linked into RetailOS** rather than being separate eApps, so launching one tests the
launcher without also testing the DRM path — and it is the clean thing to show.

The recipe is nav6's descent plus five more one-row gestures and a Select. It needed one correction
that is worth recording because it is a general trap:

> **`--stop-when-idle=400000000` cut the first attempt short.** Scrolling a list executes no code the
> widget has not already run, so the idle counter runs while the script is still going. The run
> reported `Idle after 2307665339` with `last new code @1907665339` — 400 M of scrolling — and
> stopped **one gesture and one Select before the launch**. Its final frame was `Brain Challenge`,
> row 4, a perfectly settled screen with nothing to say it had been cut off. `IDLE=2000000000` on
> the rerun.

With the window raised, the descent is thirteen gestures and every one lands where it was aimed:
five rows through the Games list is `Bejeweled · Block Breaker Deluxe · Bomberman · Brain
Challenge · Brick`, one row per gesture, in a 56-entry list — so the eight-click primitive holds on
a long scrolling list and not only on short menus.

And then **Brick draws its playfield**:

```
   ●●●                                  0   [battery]
   ▬▬ ▬▬ ▬▬ ▬▬ ▬▬ ▬▬ ▬▬ ▬▬              two rows of bricks, red over orange
   ▬▬ ▬▬ ▬▬ ▬▬ ▬▬ ▬▬ ▬▬ ▬▬
  ●                                     the ball, at the left
                                        a blue gradient field
   ▄▄▄▄▄                                the paddle, bottom left
```

Three lives as three dots, a score of `0`, the battery glyph still in the corner, eight columns of
two-tone bricks, the ball and the paddle. **That is one of Apple's own games, drawn by Apple's own
firmware, on a panel read out of this model's memory at 320x240.**

The film's timing, which is where the honesty lives:

```
   18  @2333999952   held 106000000   74063 non-black   the Games list, Brick selected
   19  @2439999952   held   8000000   74167 non-black   Select taken — the list, one detail changed
   20  @2447999952   held   2000000   76763 non-black   THE PLAYFIELD
   21  @2449999952   held 2002000000  76763 non-black   the playfield, one step on — and then nothing
```

**Two distinct frames, 2 M instructions apart, and then the panel does not change again for two
billion instructions.** `-> Idle after 4448612522` with `last new code @2448612522`: Brick's code
stopped producing novelty immediately after it drew, which is what a game loop does, and the screen
stopped changing at the same moment, which is not. The run had no further input in it.

So the measured claim is exactly this and no more: **Brick launches and draws its playfield.**
Whether it is *waiting* for the player or *stuck* is a different question, and it is settled by
giving it input rather than by staring at a static frame — §6.

`--enterlog=0x0024e808` now reports properly, and reports:

```
--- arrivals at watched addresses: 0 ---
  0x0024e808  eAppMotor  NEVER REACHED
```

A built-in game launched and the eApp task was never entered. That is consistent with research/01
Q13 — the built-ins are linked into RetailOS, not loaded as eApps — and it is now a measurement
rather than an empty section.

## 6. The wheel plays it, and the proof is a frame that comes back

The same run again with four more gestures appended, **inside the game**: `rotate=+24`, `rotate=-24`,
`rotate=+24`, `rotate=-24`, each a complete touch/rotate/release, 40 M apart, filmed every 1 M.
213 wheel frames posted, 0 dropped, every `DATA` read finding one waiting.

```
   23  @2448999952   held 56000000   frame-00023.png    the playfield as Brick drew it
   24  @2504999952   held  8000000   frame-00024.png    +24 — the paddle is moving right
   25  @2512999952   held 40000000   frame-00025.png    settled, paddle right of where it was
   26  @2552999952   held  6000000   frame-00026.png    -24 — moving back
   27  @2558999952   held 41000000   frame-00023.png    = frame 23 again
   28  @2599999952   held  6000000   frame-00028.png    +24
   29  @2605999952   held 41000000   frame-00025.png    = frame 25 again
   30  @2646999952   held  1000000   frame-00030.png    -24
   31  @2647999952   held  4000000   frame-00031.png
   32  @2651999952   held 549000000  frame-00023.png    = frame 23 again
```

**Every inverse gesture returns the panel to a byte-identical earlier frame.** Not a similar frame —
the same digest, so the same file: `+24` then `−24` lands on frame 23 exactly, twice, and the second
`+24` lands on frame 25 exactly. A paddle that moved right by some amount and left by the same amount
is back where it started, and 76 800 halfwords agree.

That is the strongest form the claim can take with a static instrument. "The screen changed" could
be almost anything; "the screen changed and then changed *back to the identical bytes* under the
opposite input, twice" is a game reading a wheel.

It also settles §5's open question: Brick was **waiting**, not stuck. And it is what the film's
repeat-detection is for — the return is invisible in a PNG sequence and unmissable in the manifest.

> **What it was waiting FOR is §10, added 2026-08-14: the centre button.** And the byte-identical
> return that makes this section's argument work is itself the tell — a screen can only come back to
> an identical earlier frame if *nothing else on it is moving*, so these eleven rows are a paddle
> moving on a playfield where the ball is still parked. Nothing here is retracted; it is a smaller
> claim than it reads as, and §10 is the larger one.

One number worth carrying: **every one of those eleven manifest rows scores 76 763 non-black
pixels** — eight distinct pictures behind them, and one identical count across all of it. Rule 2 has
never had a cleaner example.

The assembled video is 30 PNGs, a 33-entry concat list and **44.47 s** of 320x240 H.264 against the
manifest's 44.4583 s of held instructions — one output frame apart, which is what "the video's
timing is the machine's timing" has to mean to be worth saying.

---

## 7. Predictions that measured out to nothing

- **"Sixteen wheel clicks move a list one row."** Inherited from Addendum 30 §9, where it was
  measured once, and taken as a constant. It is not one. The same 16 clicks moved one row and then
  three rows *in the same run on the same list*, and two of the first three navigation attempts
  overshot because of it. What is constant is `touch, rotate=+8, release` — a whole gesture.
- **"Slowing the wheel down will make the ratio clean."** The obvious fix, and it made things worse:
  16 clicks delivered 200 000 instructions apart moved **three** rows where 16 clicks 20 000 apart
  had moved one. The rate is not the variable; whether the finger ever came off the wheel is.
- **"The non-black pixel count will tell me which screen I am on."** Two consecutive frames of the
  main menu both scored 75 791 and were different pictures. Rule 2 of this project, re-earned.
- **"`eAppMotor` will be the instrument that says a game launched."** It printed nothing, and
  *nothing* was not *zero* — `trace` suppressed the whole section for an unreached watch. The
  instrument had to be fixed before its answer could be read, and the answer (never reached, for a
  built-in) is a consistency check rather than a discriminator.
- **"A 4 G budget is enough for any script."** `BUDGET` was never the limit. `--stop-when-idle`
  was: scrolling a list runs no new code, so a long wheel script accumulates idle and the run ends
  mid-script with a screen that looks settled.
- **"Selecting `Shuffle Songs` will start playing the owner's library and put a Now Playing screen
  on the panel I would have to avoid filming."** It changed nothing at all: 742 M instructions with
  the main menu still on screen and the digest unmoved.
- **"Brick launches its ball automatically, or on the first wheel touch."** Neither. It waits for the
  centre button, and it waits indefinitely — the two billion instructions of unchanging panel in §5
  are a game correctly waiting to be told to start. (§10.1. The four *other* things that measured out
  to nothing while scripting the rally are in §10.4a, and they are more interesting than this one.)

## 8. What this opens

- **Three built-ins are untested.** `Parachute`, `Music Quiz` and `Solitaire` are in the image
  (their names and their per-language instruction strings are in OSOS) and further down the same
  alphabetical list. Only `Brick` has been launched.
- ~~**The DRM refusal is now a target with an address.**~~ ✅ **Done** — the DRM research *(not published)*
  §"What the DRM binds to, measured". The check is `FUN_00131874`; it fails at the content-key
  unwrap, with the DRM context null because the keybag yielded no keys.
- **There is no audio anywhere in this project.** The Wolfson codec is not modelled, so a game's
  sound is absent by construction and no film can show it.
- ~~**Frame rate is measurable now and has not been measured.**~~ **Measured 2026-08-14 (§10).** At a
  100 k cadence a rally gives **253 distinct pictures out of 560 samples**, so Brick redraws roughly
  every 150–240 k instructions — about 5 000 frames per second of emulator wall clock at ~21 M
  instructions/s, which is not the constraint anyone expected it to be. What *is* a constraint is
  that at `--clock=5` the game runs 14.4x fast against its own timer (§10.5).
- **No frame in this file is committed.** The screens contain Apple's UI and, for the purchased
  titles, publishers' cover art; `_out/` is gitignored and stays that way, in the same spirit as
  "no Apple product image enters the repository". Every frame quoted here is reproducible from the
  recipe beside it — §9.

---

## 9. Reproducing every screen in this file

Three commands. Each takes a few minutes and writes a PNG sequence, a manifest and an `.mp4` into
`_out/film/`. `resources/` must be present; see `tools/ipod-boot/README.md`.

```sh
# §1's control pair — filmed and unfilmed must agree in every number
BUDGET=4000000000 tools/ipod-boot/retail-boot.sh --clock=5 --stop-when-idle=400000000 \
    --bcm-registry --bcm-dump=0xE0000:140:F0:_out/film/armA.ppm
BUDGET=4000000000 tools/ipod-boot/retail-boot.sh --clock=5 --stop-when-idle=400000000 \
    --bcm-registry --bcm-film=0xE0000:140:F0:2M:_out/film/armB \
    --bcm-dump=0xE0000:140:F0:_out/film/armB.ppm
cmp _out/film/armA.ppm _out/film/armB.ppm

# The descent, as named pieces. A one-row gesture is touch / eight clicks / release.
HEAD='@1500M:touch,+2M:press=select,+5M:release'        # Select on the Language list
ROW=',+60M:touch,+2M:rotate=+8,+5M:release'             # exactly one row down
SEL=',+60M:touch,+2M:press=select,+5M:release'
TO_GAMES="$HEAD$ROW$ROW$ROW$SEL$ROW"                    # main menu · Extras · Games highlighted

# §4 — the purchased title: its cover art draws, then Apple's DRM refuses
BUDGET=5000000000 tools/ipod-film/film.sh --out=_out/film/asphalt --every=2M -- --clickwheel \
  --wheel="$TO_GAMES$SEL,+120M:touch,+2M:press=select,+5M:release"

# §5 + §6 — Brick, launched and then played
BUDGET=3200000000 IDLE=2000000000 tools/ipod-film/film.sh --out=_out/film/brick --every=1M -- \
  --clickwheel --enterlog=0x0024e808 \
  --wheel="$TO_GAMES$SEL,+150M:touch,+2M:rotate=+8,+5M:release$ROW$ROW$ROW$ROW\
,+100M:touch,+2M:press=select,+5M:release\
,+60M:touch,+2M:rotate=+24,+5M:release,+40M:touch,+2M:rotate=-24,+5M:release\
,+40M:touch,+2M:rotate=+24,+5M:release,+40M:touch,+2M:rotate=-24,+5M:release"
```

`ROW` — `touch, rotate=+8, release`, 60 M of quiet after it — is the whole of the calibration, and
the only thing in this file that has to be got right for the navigation to land. Five of them walk
`Asphalt4 → Brick`; three walk `Music → Extras`; one walks `Clock → Games`.

`IDLE=2000000000` is not optional for the Brick runs: at the 400 M default the run stops one gesture
short of the launch and says `Idle` while looking perfectly healthy.

---

## 10. Brick, actually played — the serve, the physics and the paddle

§6 established that Brick reads the wheel, by the strongest form the instrument allowed: an inverse
gesture returning the panel to a byte-identical earlier frame, twice. What it could not establish is
that the *game* was running, because **the ball never moved in any of those runs** — a returning
digest is only possible if nothing else on screen has changed, and the reason nothing else changed is
that Brick was sitting in its pre-serve state the whole time.

Everything below is measured at a **100 k cadence** — the first one fine enough to see the ball
between two positions — which needs `--bcm-film-from`, because 100 k over the 2.4 G of boot in front
of the game is twelve thousand surface scans of screens nobody is looking at.

### 10.1 The centre button serves; nothing else does

Three candidate inputs, 200 M apart so each one's consequence is attributable to it alone, on a
freshly launched Brick:

| input | result |
|---|---|
| `press=select` | **the ball leaves its rest position 1 M instructions later** |
| `press=play` | nothing. 404 M instructions with the panel's digest unmoved |
| `touch, rotate=+16, release` | the paddle moves. The ball does not |

So §5's *"whether it is waiting for the player or stuck is a different question"* is answered: it was
waiting, and it was waiting for the centre button.

**The ball does not rest on the paddle.** It sits at **(4, 130)** — the left edge, mid-height — while
the paddle sits at the bottom left, and research/13 §5's sketch had that right without knowing it
mattered.

### 10.2 The physics is a 45° reflection, and it is exactly predictable

The ball moves **(±8, ±10) pixels per game tick**, and every collision measured flips exactly one
sign:

| event | in | out |
|---|---|---|
| paddle at (66,203) → (99,203) | +8, +10 | +8, **−10** |
| brick at (216,66) → (224,66) | +8, −10 | +8, **+10** |
| right wall at (315,175) | +8, +10 | **−8**, +10 |

**Where on the paddle the ball lands does not change the outgoing angle** — this is not the
Breakout-style paddle that steers the ball. That makes the whole trajectory computable from the
serve, which is what makes a scripted rally possible at all.

A tick is **150–240 k instructions** and the spread is real rather than measurement noise: one leg of
the first rally holds a single frame for 1.4 M while the rest step every 100–200 k, so something else
on the machine takes the CPU mid-rally. From serve to the ball crossing the paddle's row is **2.4 M
instructions**; a full paddle → bricks → paddle leg is **6.1 M**; an unreturned ball is lost **9.1 M**
after the serve.

**That is why a rally cannot be played with the discrete gestures the menus use.** One
`touch, rotate, release` costs 7 M instructions and the ball is gone in 9 M.

### 10.3 The paddle moves in 24-pixel quanta

The paddle is **57 px wide** and its left edge travels **[4, 262]**, clamped hard at both ends —
two 16-click bursts against the right wall move it not at all, the same no-wrap behaviour the menus
show.

| gesture | paddle moves |
|---|---|
| discrete `touch, rotate=+8, release` | **+24 px** |
| discrete `touch, rotate=+24, release` | **+120 px** — five quanta, in two visible steps |
| held gesture, `rotate=+2` every 400 k | **+24 px per step**, steps landing ~750 k apart |

So the quantum is 24 px and **the click count does not buy pixels** — eight clicks and two clicks
both move one quantum. What decides how many quanta a burst produces is the same accelerator §2.2
found in the menus, and this is the second independent sighting of it. The practical rule for a
script is the third row: **one held `rotate=+2` step is one quantum, and the game will take about
750 k instructions to act on it**, which caps the paddle at roughly 32 px of travel per million
instructions.

Against a 6.1 M leg that is ~195 px of reach, and the ball needs 190 px of it. The rally in §10.4 is
therefore not comfortable — it is scripted against the clock with almost nothing to spare, and that
is a property of the game at `--clock=5`, not of the instrument.

### 10.4 A rally

The recipe is in [`tools/ipod-film/post-assets.sh`](../tools/ipod-film/post-assets.sh), which is the
tool that produces the film, so the script and the write-up cannot drift apart. It is **two paddle
sweeps and one button press**, and it took five attempts to get there — the interesting part is
which four things were wrong.

The rally it produces, read off the film by locating the ball and the paddle in every frame:

The rally it produces, read off the film by locating the ball, the paddle and the score in every one
of its 253 frames. A **return** is the ball vanishing behind the paddle for a few samples and coming
out going the other way; the paddle position is the one measured in the frame before it.

| @ | what | paddle |
|---|---|---|
| 2 578.6 M | `press=select` — the serve. 3 lives, **score 0** | 52-108 |
| 2 579.6 M | the ball leaves (4,130) heading down-right | |
| **2 580.8 M** | **return 1** — in at (66,203), out at (99,203) going up-right | 52-108 |
| 2 583.3 M | the paddle finishes its sweep to the right travel limit | 52 → **262-318** |
| 2 585.5 M | **brick** | |
| **2 589.2 M** | **return 2** — in at (294,200), out at (255,210) | 262-318 |
| 2 592.1 · 2 592.3 M | **two bricks** | |
| **2 594.7 M** | **return 3** — in at (255,200), out at (315,200) | 262-318 |
| 2 597.6 M | **brick** | |
| **2 600.0 M** | **return 4** — in at (315,200), out at (259,200) | 262-318 |
| 2 602.5 M | the second sweep lands, and overshoots: five steps go 262 → 22, not 262 → 142 | **22-78** |
| 2 603.8 · 2 605.4 M | **two bricks** on the left, then the left wall at (9,90) | |
| 2 608.2 M | the ball drops at x=173, past the paddle's right edge. **Life lost** | 22-78 |
| 2 608.6 M | **the next ball serves itself** — no button press | 2 lives |
| **2 611.3 M** | **return 5** — the overshoot put the paddle exactly where this ball comes down | 22-78 |
| 2 614.2 M | **brick** | |
| **2 621.2 M** | **return 6** — in at (66,203), out at (84,225) | 22-78 |
| 2 624.3 M | **brick** | |
| 2 627.3 M | the ball drops at x=90, twelve pixels past the paddle | |

**Six returns and eight bricks**, and the film's last frame is the whole claim in one picture: the
score reads **8**, eight gaps have appeared in a wall that started solid, and the three life dots
have gone grey. 560 samples collapse to **253 distinct pictures** — a film where almost every sample
is a new frame, which is the opposite of every other film in this project.

**The paddle's second sweep overshooting is not a defect that was left in — it is the reason the film
has a fifth and sixth return in it.** It was aimed at x=173 to catch ball 1, missed because five
`rotate=-8` steps travel 240 px rather than the 120 the first sweep's acceleration predicted, and
parked the paddle exactly where the *next* ball comes down. Recorded as it happened rather than
tidied into a plan.

**The auto-serve is worth its own line.** Only the first ball needs the centre button; after a life
is lost the next one launches itself about 1.5 M instructions later, from the same rest position,
on the same trajectory. So a script only has to serve once.

### 10.4a Four things that were wrong on the way, and one that was right

- **"The paddle should chase the ball down."** It cannot: `rotate=+2` steps 400 k apart move it
  about 29 px per million instructions and the ball needs 40. Attempt 1 chased and arrived 158 px
  short. What works is `rotate=+8` steps **200 k** apart, which move it **150 px per million** — the
  accelerator is rate-sensitive, and five times the speed was available the whole time for a
  smaller-looking gesture.
- **"The landing points alternate left, right, left."** They do for exactly two legs, and the
  reasoning behind it — the reflection is symmetric, so the ball must come back to the mirror of
  where it left — is undone by the bricks. A brick taken out of the left of the wall sends the ball
  back down the **same** side, and attempt 3 had already swept the paddle to the other one.
- **"The ball's speed is a constant."** It is not: after the second return the horizontal step goes
  from 8 px to 12 px per tick and stays there. Every timing derived from the first leg is 50 % out
  by the third.
- **"A wheel script is inert with respect to the game's timing."** It is not, and this is the one
  that made every attempt need a re-measurement. Wheel traffic costs instructions, so a heavy sweep
  delays everything downstream of it: the same brick is hit at 2 584.9 M with a light script and
  2 586.5 M with a 24-step sweep in front of it. **A rally script has to be re-read off its own run,
  not adjusted on paper.**
- And the one that held: **where the ball lands on the paddle does not steer it.** Every return in
  the table above is a clean vertical flip, which is why an open-loop script can play this at all.

### 10.4b The A/A that had to be run, and what it caught

Two films built from what were supposed to be identical scripts came out with **253 and 249 distinct
pictures**, diverging at exactly 2 582.1 M — while the machine underneath them reported the *same*
instruction count, the *same* 39 549 code buckets and the *same* 712 ATA commands. That reads as
nondeterminism in the emulator, which would make every film in this repo a sample of a distribution
rather than a measurement, so it was worth stopping for.

**The A/A first, because the alternative is arguing about it.** Same command twice,
`BUDGET=2600000000 retail-boot.sh --clock=5 --stop-when-idle=2000000000 --bcm-registry --clickwheel
--wheel=<the rally>`, different processes, different per-run disk clones:

```
-> BudgetExhausted after 2599999952      39 534 code buckets      712 ata commands
irqs 9025491 asserted, 1571749 taken; usec 3354610189
cpu sleep: 7141383 halts, 2834610 ms skipped
clickwheel: 212 frames posted (0 dropped unread)
cmp aa-1.ppm aa-2.ppm    -> silent
```

**Identical in every field, and the framebuffers are byte-identical.** The emulator is
reproducible.

The divergence was **mine**: the `sweep` helper in `post-assets.sh` still spaced its steps 400 k
apart from an earlier take while the calibration had moved to 200 k, and §10.3's whole point is that
those are different speeds — 29 px per million against 150. Two scripts that read the same in a
diff of the shell produced schedules that differ from step 11 onward, which the run report prints in
full and which is where it was found.

**The lesson is the one R5 keeps making in a new place: a control has to match the measurement in the
thing that varies.** Here the two runs matched in every machine counter *because the machine is
deterministic*, and matching machine counters said nothing at all about whether the two runs had
been given the same input. The run report's expanded wheel schedule is the artifact that answers
that, and it is printed on every run precisely so a log reproduces itself.

### 10.5 Playback rate — the one honest choice a gameplay film has to make

`--rate` is instructions per second of video, and the boot films use **72 000 000**, a PP5021C's
rate, so playback matches the pace real silicon would have executed those instructions. **For
gameplay that is the wrong number**, and it is wrong by a factor of 14.4.

`--clock=5` advances simulated time by one microsecond every five instructions, against real
silicon's 72 instructions per microsecond. Brick's animation is driven by the *firmware's* clock, so
at `--clock=5` the game runs 14.4x faster in instruction terms than it would on hardware — a ball
crossing the panel in 1.3 M instructions rather than 19 M. Played back at 72 M/s the whole rally is
over in under a second and is unwatchable.

**`--rate=5000000` makes one second of video one second of the machine's own simulated time**, which
is the rate the game's timer thinks it is running at, and the ball crosses the screen at the speed
Brick intended. It is not a slow-motion effect and it is not a correction applied to make the film
look better: it is the same choice the boot films make (play at the pace of the clock that drives the
thing being filmed) applied to a clock that is not the CPU's.

## 2026-08-18: the built-ins are not eApps, and the framework surface is enumerable

Two questions settled in one pass — and they point in opposite directions.

### The stock games are not eApp containers. Definitively.

| scan | result |
|---|---|
| `"eapp"` across `OSOS_correct.bin` (7 559 680 B) | **exactly one hit, `0x122508`** — the loader's own literal pool |
| block magic `68 19 06 29` across OSOS | **exactly one hit, `0x122510`** — the same literal pool. **Zero** import blocks anywhere |
| `"eapp"` + `Brick`/`Parachute`/`Music Quiz`/`Solitaire`/`BlockO`/`Chopper` in the `rsrc` volume | **0 hits, all seven** |
| the same needles in `aupd.bin`, `flsh/{diag,disk,scan,vmcs}.bin` | **0 hits** |

`FUN_001222c4` — the eApp header validator, whose literal pool those two hits *are* — has exactly
one caller, `0x0024e420`, inside the eApp subsystem. §"do the games load" already recorded
`eAppMotor` (`0x0024e808`) as **NEVER REACHED** when launching Brick. Static and dynamic agree: the
eApp loader is not entered for a built-in.

Decisive by absence too. The eApp packaging vocabulary is compiled into OSOS at
`0x00678d24`–`0x00678df8` — `GUID`, `BuildID`, `ExecutablePath`, `HeapSize`, `DRMLevel`,
`Manifest.plist.p7b`, `.sinf`. A built-in has none of them.

**They are plain compiled-in code.** Their titles sit interleaved with ordinary firmware menu rows
in the same flat string pool — `"Clock"`, `"Unknown Error"`, `"Parachute"`, `"Music Quiz"`,
`"Brick"`, `"Screen Lock"` — reached **by ordinal** into one of 24 language blocks (base table
`0x0069cce4`). Apple's internal names are **`BlockO` = Brick, `Chopper` = Parachute**.

### So "extract a stock game" means "reimplement the host"

|  | purchased eApp | built-in game |
|---|---|---|
| container | `eapp` header, block table, own load base `0x18000000` | none — plain `.text` in `osos` |
| resources | its own directory of files | none; shared string pool + shared view-descriptor table `0x004cc188` |
| host coupling | **explicit**: named imports patched into `ldr pc,[pc,#N]` thunks | **implicit**: ordinary `bl` into arbitrary firmware internals |

There is no boundary to cut along: no relocation, no import table, shared widget toolkit, shared
display path (`0x001650f8` → `0x00164f44` → `0x0028861c`), shared settings store, shared event
system, shared allocator, no task of its own. Nothing in the image even points at `"Brick"` — the
strings are reached by ordinal, so a pointer scan into the title range returns **0 hits**.

### The tractable direction is the other one, and it is now bounded

RetailOS **publishes** a self-describing, content-hash-versioned surface at
`0x000793fc`–`0x00079ce0`. Records are keyed on the name pointer: `+0x24` count, `+0x28` next,
`+0x2c` the function-pointer array, then the 16-byte interface hash, then the next record's magic
`0x13061973`.

| framework | functions | interface hash |
|---|---|---|
| `miscTBD` | 15 | (head record) |
| `OpenGLES` | 179 | `041f4da520603c37d8ef9b879efbf280` |
| `Filesytem` *(Apple's typo, preserved)* | 4 | `9aff0ee8f4485b4006ef4c0e578c3c0c` |
| `Audio` | 61 | `d9c859f2325e3831f974c1218c35672d` |
| `Metadata` | 152 | `71def8ce4eedefcd5b42dc23a5f45d4d` |
| `AsyncFileIO` | 17 | `0012f0601105c3a75125314e17aa989a` |
| `InputEvents` | 2 | `c73357d0487174bd02f378e54437e1e6` |
| `Settings` | 3 | `91e11cfe3c1f4eda92a085db26c09dba` |

**8 frameworks, 433 functions** — and those hashes are byte-identical to the ones `eapp-inspect`
prints from Pac-Man's own import blocks, which is independent confirmation of the ABI from both
sides at once. Pac-Man declares **98** of the 433.

That is the shape of the "run a title with no Apple OS" work: not an unbounded reimplementation,
but a published interface with a known size, a per-title subset that is enumerable before any code
is written, and a hash that says when we have got it wrong.

**Open.** The entry address of Brick's own game loop is still unknown, and there is no static handle
— no RTTI, no symbols, strings by ordinal. The cheap way to name it is a dynamic diff of the two
runs in §"do the games load" (stop at the Games list vs. launch Brick); that run recorded
`last new code @2448612522` without printing the address.
