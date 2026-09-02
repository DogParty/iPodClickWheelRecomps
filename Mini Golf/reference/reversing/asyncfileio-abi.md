# The iPod 5G framework ABI, as far as it is measured

Working notes for porting click-wheel games off the iPod. Everything here is measured against two
things at once — Apple's own implementations in `osos`, and Minigolf's behaviour under
`eapp-loader` — and where those two disagree with an earlier guess, the guess is recorded as wrong
rather than quietly dropped.

Target used throughout: **Minigolf** (`Games_RO/88888`, `Minigolf_1_1_2563296.bin`, 227 868 bytes,
load base `0x18000000`).

## 1. The frameworks are published by the OS, and enumerable

`osos` carries a self-describing table. Extract it from the IPSW at **offset `0x4600`, length
`7 559 680`** (`Firmware-20.6.3` inside `iPod_20.1.3.ipsw`).

Records are keyed on the **name pointer** `N`:

| field | offset |
|---|---|
| name (NUL-terminated, 32-byte buffer) | `N` |
| count | `N+0x24` |
| next record's name pointer | `N+0x28` |
| function-pointer array, **inline**, `count` entries | `N+0x2c` |
| 16-byte interface hash | `N+0x2c + 4*count` |
| next record's magic `0x13061973` | after the hash |

Walking it yields **8 frameworks, 433 functions**:

| framework | count | fn array | Minigolf imports |
|---|---:|---|:--:|
| OpenGLES | 179 | `0x000794a8` | ✓ |
| Filesytem *(Apple's typo)* | 4 | `0x000797b8` | |
| Audio | 61 | `0x0007980c` | ✓ |
| Metadata | 152 | `0x00079944` | |
| AsyncFileIO | 17 | `0x00079be8` | ✓ |
| InputEvents | 2 | `0x00079c70` | ✓ |
| Settings | 3 | `0x00079cbc` | ✓ |
| miscTBD | 15 | head record | ✓ |

Every interface hash matches the ones in Minigolf's own import blocks, so the implementations the
game calls are the ones in this image. All 433 addresses are in `framework-functions.json`.

**Why this matters for a port:** these 98 functions are *not* in the game binary — it carries only
`ldr pc,[pc,#imm]` thunks. They cannot be recovered from the game at any effort level. `osos` is
the only source of their semantics, and a port has to reimplement them.

## 2. AsyncFileIO is asynchronous, and that is not optional

Each entry in the table is a thin shim: fetch a singleton, `ldr r0,[r0,#0x268]`, tail-call the real
implementation. Argument shuffling differs per entry, so the request object lands in a different
register for each:

| import | shim | real impl | request object in |
|---|---|---|---|
| `#0` open | `0x002680e4` | `0x001e3310` | game `r3` (passed 5th) |
| `#1` | `0x002680c8` | `0x001e33a0` | game `r0` |
| `#2` read | `0x00268144` | `0x001e36c8` | game `r0` |
| `#3` open | `0x00268118` | `0x001e38cc` | game `r2` |
| `#4` | `0x00268160` | `0x001e3958` | game `r2` |

The open at `0x001e3310` reads the request from the fifth argument, requires
`request->state == 1`, checks two arguments are non-zero, then **allocates and enqueues** the
operation and returns non-zero. It never touches the file inside the call.

The read at `0x001e36c8` requires `request->state ∈ {3,4,5}` and otherwise returns 0 — a silent
refusal, no error anywhere.

So the contract is: **accept the operation, return non-zero, and call the game's completion
callback later.** A synchronous implementation cannot satisfy it, whatever it returns.

### The request object

Read out of a live request while Minigolf had one in flight, and corroborated against the fields
Apple's implementations actually load:

| offset | meaning | evidence |
|---|---|---|
| `+0x04` | state byte | host gates on it: open wants 1, read wants 3/4/5 |
| `+0x08` | the game's file object | set by `0x180183b0` before the call |
| `+0x14` | buffer | |
| `+0x18` | length | held `0x4c` = 76 = `jdmgsheets0`'s exact size |
| `+0x20` | status; 0 = success | the completion reads it and branches |
| `+0x2c` | stream id the host installs | completion writes it to `file_obj[0]` |
| `+0x34` | completion callback | |
| `+0x38` | callback context | |

### The completion callback takes two arguments

`callback(request, context)`, where `context == request - 0x128` for the read path. The read
completion at `0x18017574` asserts `arg0 == arg1 + 0x128` and, when it fails, executes
`b .` at `0x180175d0` — an infinite self-loop inside the game's own code, with no diagnostic. A
one-argument call hangs there.

Minigolf contains **354** such `b .` assertion traps. They are the game's panic handler, and
finding which one is spinning is the fastest way to diagnose a stall (`--enterlog` over the list).

Client-side state machine, from Minigolf's call sites — the state advances only when the host
returns **non-zero**:

```
init (0x180182c4) -> 1
  #0 open  ok -> 1   (0x18018050)   completion -> 2  (0x18017f34, status 0)
  #1       ok -> 3   (0x180180e4)
  #3       ok -> 4   (0x180181c0)
  #4       ok -> 5   (0x1801827c)
  failure     -> 0   (0x18017f98)
```

## 3. The convention that was backwards

`Stub::FileOpen` returned 0, and the loader's comment said *"Zero is success in the calling
convention seen so far."* That holds for Pac-Man's `Filesytem #0`. It is **inverted** for
Minigolf's `AsyncFileIO #0`, whose call site at `0x18018044` is:

```asm
18018044  bl   0x18000638          ; AsyncFileIO #0
18018048  movs r6, r0
1801804c  movne r0, #0x1
18018050  strneb r0, [r4, #0x4]    ; state = 1, only when non-zero
18018054  bne  0x1801806c          ; ... and return
18018058  mov  r0, #0x0            ; else: the failure path
1801805c  str  r0, [r4, #0xc]
18018060  str  r0, [r4, #0x10]
18018068  bl   0x180184b4          ; free the request, abandon the load
```

Returning zero frees the request and gives up, with no error and no visible change: the title
screen stays up forever. This is the whole reason the game appeared frozen.

## 4. InputEvents

`InputEvents #0` is a poll with the event word written to `*(arg0 + 4)`. The game reads it at
`0x18018964` and decodes:

```asm
1801896c  tst r0, #0x40000000      ; bit 30 = an event is present
1801897c  and r0, r0, #0xff        ; low byte = the code
```

which is exactly the loader's existing `0x4000_0000 | code` encoding. Confirmed by sweeping all
256 codes: every one is delivered and decoded. **No input code has any effect on the title
screen** — it is not waiting for a button, so input is not the way past it.

## 5. Current state

With the async model implemented (`--async-files`), Minigolf **loads completely**. 53 file
operations, in the game's own order:

```
jdmgp.sav / jdmgp2.sav   MISS      (no save file present — correct)
jdmgsheets0              76
jdmgsheets               518 124 + 407 720 = 925 844   (chunked, exact file size)
jdmg0 / jdmg             556 / 420 591
jdmg0.en / jdmg.en       1 572 / 3 170
c00sheets0 / c00sheets   44 / 788 477
c000 / c00               12 140 / 2 022 894            (four chunks)
c000.en / c00.en         292 / 344
c00bank/0.wav … 9.wav    all ten
```

That is the title assets, the localized strings, and **all of course 00** — roughly 4 MB of the
game's own data. Audio init reaches 10 imports, `miscTBD` 7, and 40 of Minigolf's 98 imports are
now exercised (up from 29). It sustains 29 fps.

The earlier stall at `0x18016b5c` is gone. Its cause was **`AsyncFileIO #1` being unstubbed**:
Apple's implementation at `0x001e33a0` accepts it only when `request->state == 2` — the value the
open's completion leaves — and the game advances to state 3 only on a non-zero return. An
unstubbed import returns 0, so the sequence died one step after the open and left the stream
object busy, which is what the assertion was complaining about. `Stub::AsyncOp` fixed it, and file
objects are now visibly recycled across loads (`0x1900099c` and `0x19000b20` alternate), which is
the close/idle cycle working.

### Rendering: mode 7 is a quad list, not a fan

The first frames after the load were the title logo plus coloured streaks across the screen. The
cause was one line in `draw_arrays`: everything that was not `GL_TRIANGLE_STRIP` was fanned from
vertex 0.

**Mode 7 is Apple's quad list** — vertices in independent groups of four. A fan and a quad list
produce the same two triangles when `count == 4`, which is why every title-screen draw looked
correct and hid it completely. Minigolf's course draws arrive as `n=16, 20, 28, 36`, and fanning
those stretches triangles between unrelated quads. Fixing it, and raising the 64-vertex ceiling
that silently dropped larger draws, renders the game correctly:

> **MINI GOLF** · *PLEASE ENTER YOUR NAME. (MAX. 16 CHARS)* · `A B C D E F G H I J K ▶`

Positions were never wrong — they arrive in pixel space (`[10..310, 10..155]`), `GL_FIXED` 16.16 as
`attr()` already assumed, and textures upload fine (`tex#0/#2/#4/#5` all in use). Only the
primitive assembly was.

Of the 17 OpenGLES entries the game calls, 7 are implemented and the rest are harmless no-ops:
`#53` is `glGetError` (called after every single call — returning 0/`GL_NO_ERROR` is correct),
`#40`/`#36` are `glEnable`/`glDisableVertexAttribArray`, `#101` is a `glTexParameter` family call
(`0x84F5` = `GL_TEXTURE_RECTANGLE`, `0x8066`, `1.0f`), and `#167`/`#165` carry `0x43a00000` = 320.0f,
so they are viewport/projection setup that the pixel-space assumption makes redundant.

## 6. Open: input

The game renders its name-entry screen and idles there, redrawing every frame (12 405 quads over
1500 frames) but never changing — byte-identical output at frames 400, 800 and 1500.

Input reaches the game and is dispatched, and the encoding is fully understood:

- `InputEvents #0` is `poll(&out0, &out1)`; the game reads **`out1`** at `[sp+4]`, tests
  `bit 30` for "event present" and takes the **low byte**.
- `0x18011644` → `0x180135c8` does `mov r0, r0, lsl #16` and stores the result to two globals.
  The low byte is therefore a **wheel position as 16.16 fixed point**, not a button id.
- A 16-entry ring at `[r9+0x10]` counts how many of the last 16 polls carried an event.
  `[r9+0x14]` is a flags word: `0x20` = event this poll, `0x40` = ring count > 1 (sustained
  contact). Non-zero flags dispatch to `0x18011528`.

### A gate found, and then ruled out: bit 0 of the flags word

> ⚠️ **Read the end of this section before using it.** Bit 0 looked like the answer and is not:
> forcing it, once and then held across 150 frames, changes nothing. It is recorded because the
> path is real and the elimination is worth having, not because it is the cause.

The dispatcher `0x18011528` → `0x18013fac` publishes the input state to a block at `0x1804050c`:

| address | field | written by |
|---|---|---|
| `0x1804050c` | *(never written by anything)* | — |
| `0x18040510` | flags | `0x18013fac` |
| `0x18040514` | wheel position, 16.16 | `0x180135c8` |
| `0x18040524` | dispatcher's second arg | `0x18013fac` |

`0x18013690` snapshots that block, and its caller does:

```asm
1801158c  add r0, sp, #4
18011590  bl  0x18013690        ; snapshot the input block
18011594  ldmib sp, {r0, r5}    ; r0 = [0x1804050c], r5 = flags
...
180115ac  tst r5, #0x1          ; <-- THE GATE: bit 0 of the flags
180115b0  strne r6, [r4, #0x14]
```

**The UI acts on bit 0, and nothing in the measured input path ever sets it.** Watching the flags
word (`0x18037a0c`, i.e. `[r9+0x14]`) across 400 frames of continuous input gives 801 changes and
exactly two writers:

```
0x18018a40  0x00000000 -> 0x00000020   strne r0, [r9, #0x14]   (event this poll)
0x18018a24  0x00000020 -> 0x00000000   str   r0, [r9, #0x14]   (cleared next poll)
```

Bit 5 (`0x20`) and bit 6 (`0x40`, sustained contact) are the only bits the poll handler manages —
`bic r0, r0, #0x60` deliberately preserves everything else, so **bit 0 is expected to arrive from
somewhere else**. A scan of all 29 `str rD,[rN,#0x14]` sites in the image finds no `orr #1` feeding
any of them, so it is not set by a path we simply failed to reach through this handler.

**But bit 0 is not sufficient.** Poking it into `[r9+0x14]` — where `bic r0,r0,#0x60` preserves it,
so it does propagate to `0x18040510` — produces a byte-identical frame, both as a single write and
held across 150 consecutive frames. So `tst r5,#1` is a real branch on a real flag, and it is not
what this screen is waiting for. Poking the context byte at `[r5+0]` (values 1 and 2, at two
different frames) is likewise inert.

### The event word carries no buttons — settled from Apple's own code

`InputEvents #0`'s entry at `0x00268320` is `b 0x001181f8`, and that is the real poll:

```asm
001181f8  ...
00118204  ldr r1, [r4, #0x14]     ; last-read counter
00118208  ldr r5, [r4, #0x10]     ; current counter
0011820c  sub r1, r5, r1          ; delta
00118210  str r1, [r0, #0x0]      ; *out0 = events since the last poll
00118214  ldr r0, [r4, #0x24]
00118218  bl  0x000e95a4          ; encode
0011821c  str r0, [r6, #0x0]      ; *out1 = the event word
```

and the encoder at `0x000e95a4`:

```asm
000e95a8  and r4, r0, #0x40000000   ; keep bit 30 = "present"
000e95ac  bic r0, r0, #0x40000000
000e95b0  rsb r0, r0, #0x77         ; 0x77 = 119 -> a 120-position wheel, inverted
000e95b4  mov r0, r0, lsl #3        ; *8
000e95bc  bl  0x0007d024            ; /3
000e95c0  and r0, r0, #0xff
000e95c4  orr r0, r0, r4            ; re-attach bit 30
```

So the word is exactly **`bit30 | transformed_wheel_byte`, and nothing else** — no button bits
exist in it. `out0` is a *delta*, the count of events since the previous poll; Minigolf never reads
it (the only stack reads in the whole handler are `[sp+4]`, three times). Our stub therefore models
this import correctly and completely, which is why no wheel pattern has ever mattered.

### The real blocker: the frame vector's context is synthetic

The poll handler opens by saving both context arguments and then depends on their fields:

```asm
18018920  mov r4, r0        ; context 0
18018928  mov r5, r1        ; context 1
18018930  ldrb r0, [r5, #0x0]      ; branches on a byte we always leave zero
...
18018a50  ldr  r1, [r4, #0x30]     ; handed straight to the input dispatcher
18018a54  bl   0x18011528
```

`--ctx` hands the game two **zeroed scratch buffers** where RetailOS passes real structures. So
`[r4+0x30]` — which the dispatcher publishes to `0x18040524`, one of the three words the UI reads
every frame — is always 0, as is the byte at `[r5+0]` that selects between two paths in the poll
handler.

That is the most likely reason a fully-correct input word changes nothing: the UI is reading a
state block whose third field is a null pointer it was supposed to receive from the OS.

Poking those fields does **not** unblock it either. Sustained across frames 200–400, with a wheel
ramp running, each of the eight fields the poll handler actually touches — `ctx1+0x00/0x20/0x24/
0x28/0x29` and `ctx0+0x2c/0x30/0x34` — set to `0x01` and to `0xff`, produces a byte-identical
frame. So a single missing scalar is not the answer; if the context is the cause, the game needs
something structured, not a non-zero flag.

### What the steady state actually looks like

Profiling the eApp path (the `--profile` flag was only honoured inside the boot branch and silently
did nothing here until fixed) shows **125 924 samples across 1 283 buckets, the hottest at 4.1 %**.
That is a diffuse, healthy render loop doing real work — not a spin, not a wait loop. The game is
running its UI every frame and simply never changes state.

**Next step:** reverse the eApp context structure from RetailOS's own launcher. The entry points
are known — `FUN_001222c4` is the eApp header validator, its only caller is `0x0024e420` inside
the eApp subsystem, and `eAppMotor` is the task at `0x0024e808`, whose teardown confirms the eApp
object layout (`[+0x268]` is the async-IO subsystem, exactly the field the `AsyncFileIO` shims
load). The eApp load region is configured at `0x000df0e4`: base `0x18000000`, size `0x100000`.

Note this cannot be traced dynamically — RetailOS only launches an eApp on a real boot, and that
needs a NOR dump this project does not have. It has to come out of static reading.

## 7. The eApp object, as far as it is read

Started on it. What is established, from the loader at `0x0024e380`–`0x0024e4a0` and `eAppMotor`:

| offset | meaning | evidence |
|---|---|---|
| `+0x21c`, `+0x224`, `+0x238` | sub-objects destroyed on teardown | `eAppMotor` `0x0024e808` |
| `+0x230`, `+0x231` | state bytes written around the lifecycle hooks | `0x0024e15c`, `0x0024e18c` |
| `+0x249` | mode byte: 1 or 2 depending on which vectors exist | `0x0024e470`–`0x0024e490` |
| `+0x24c` | base of the vector table handed to the validator as `r2` | `0x0024e41c` |
| `+0x250` | vector 1 — called at `0x0024e304` / `bx` at `0x0024e30c` | |
| `+0x254` | vector 2 — called at `0x0024e178` / `bx` at `0x0024e184` | |
| `+0x258`, `+0x260` | vectors 3 and 5, tested for null to pick the mode byte | |
| `+0x264` | freed on teardown | |
| `+0x268` | **the AsyncFileIO subsystem** — exactly the field every `AsyncFileIO` shim loads with `ldr r0,[r0,#0x268]` | |

The load sequence is: DRM check at `0x00131874` (research/13's function, reached from `0x0024e3c0`),
and only on success the header validator `0x001222c4`, which checks `"eapp"`, version `0x10001000`,
block count ≤ 5, then walks and resolves the framework import blocks by their `0x13061973` /
`0x29061968` magics. The eApp load region is configured at `0x000df0e4`: base `0x18000000`,
size `0x100000`.

### The manager is a singleton, and it owns a task

`0x0024d88c` is a guarded C++ singleton accessor — the one every framework shim calls before
`ldr r0,[r0,#0x268]`. The object lives at **`0x108711d4`**, its constructor is `0x0024e718`, and
**`eAppMotor` is its destructor**, registered through `__cxa_atexit`. 27 call sites reach it.

The constructor allocates the async-IO subsystem into `+0x268` (`0x001e41a4`), a second object into
`+0x264`, and then creates a **task** via `0x0011c808` — entry `0x0024d87c`, stack `0x3800`,
priority `0x32`. That task is the eApp's run loop:

```
0x0024d87c  -> 0x0024d88c (get the singleton) -> 0x0024e554
0x0024e554  loop: wait; if a game is queued -> 0x0024de30, 0x0024e0dc; else park; cleanup; repeat
0x0024e0dc  the frame pump: while 0x0024e61c(mgr) -> 0x0024da80(mgr), 0x0024dce8(mgr)
```

### The context, settled

`0x0024da80` is where the frame vector is called, and it gives the whole answer:

```asm
0024dabc  ldr  r5, [r4, #0x260]   ; THE FRAME VECTOR — +0x260, not +0x25c
0024dac0  cmp  r5, #0x0
0024dac4  ldmiaeq sp!, {r4-r6, pc} ; null -> nothing to do
0024dac8  ldr  r0, [r4, #0x268]   ; the AsyncFileIO subsystem
0024dacc  bl   0x001e3c14         ; query it
0024dad0  str  r0, [r4, #0x2c]    ; -> ctx+0x2c
0024dad4  ldr  r0, [r4, #0x26c]
0024dad8  str  r0, [r4, #0x30]    ; -> ctx+0x30
0024dadc  ldrb r0, [r4, #0x230]   ; 1 -> 5, 3 -> 4
0024daf8  strb r0, [r4, #0x0]     ; -> ctx+0x00, a state byte
0024dafc  add  r1, r4, #0x100     ; SECOND ARGUMENT
0024db00  mov  r0, r4             ; FIRST ARGUMENT = the manager itself
0024db04  mov  lr, pc
0024db08  bx   r5
```

**The context is the eApp manager object, and the second argument is a pointer 0x100 into the same
object — not a second buffer.** That is what `--ctx` had wrong: it passed two independent scratch
blocks, so every field the game reaches through both arguments was disjoint when it should alias.
Fixed in both `play` and `trace`.

It also lines up field-for-field with Minigolf's poll handler, which is the confirmation that
matters:

| game reads | pump writes |
|---|---|
| `ldrb r0,[r5,#0x00]` (`0x18018930`) | `ctx+0x100` |
| `ldr r0,[r4,#0x2c]` (`0x18018a60`) | `0x001e3c14(asyncio)` |
| `ldr r1,[r4,#0x30]` (`0x18018a50`, handed to the input dispatcher) | `[mgr+0x26c]` |
| `ldrb r0,[r4,#0x00]` | the 5/4 state byte |

Note `[mgr+0x26c]` is **0** at construction, so `ctx+0x30` being null is correct rather than the
missing piece it looked like.

**And the frame vector is `+0x260`, i.e. image `+0x24`** — which is exactly the slot `play` had been
calling on the empirical "last non-zero vector" rule. That rule turns out to be right, and the
earlier `+0x25c` scan found nothing because it was looking one slot short.

Two anchors that did **not** pan out, worth not repeating: the image-base global `0x002a93cc` is
referenced from exactly one literal pool (the loader's own), and scanning for header-relative
vector loads (`+0x14`…`+0x24` followed by a branch) returns 1008 hits, all ordinary vtable dispatch.

### A tool bug found on the way

`trace.rs` had `RETAILOS_EAPP_LOADER = 0x1012_24C4`. `0x001224c4` is the `bne` **failure exit inside**
the validator, not its entry (`0x001222c4`). `--run-loader` was therefore executing four
instructions — `mvn r0,#1; b …; add sp…; ldmia` — returning −2 and reporting "0 of 277 thunks
patched" as a measurement. Corrected to `0x1012_22C4`; it now runs 159 instructions and walks
Apple's real framework registry, string-comparing `"OpenGLES"` against the table at `0x000793fc`.
It still exits −3, which is a genuine check failing and worth chasing.

### Warm boot does not avoid the NOR

Tried, and it does not work: RetailOS *does* execute warm from an extracted `osos.bin` (1.2 G
instructions, 110 981 interrupts, and with an IPSW-built drive it reaches the disk — 8 IDE IRQs),
but it parks in a 100 ms delay loop and drops a 32 KB DMA to a garbage address. The reason is in
this project's own ledger: warm entry keeps **NOR mapped at 0** because RetailOS's scatter-load
depends on it (#11, still 🟡 for warm), and `--sysinfo` installs a Gestalt ID the warm path reads
*out of the NOR dump* (#5). `warm-boot.sh` passes `--flash` for exactly those reasons. Warm entry
relocates the NOR requirement rather than removing it.

Two further candidates, now lower priority:

1. **The first out-param.** `InputEvents #0` is `poll(&out0, &out1)` and we only ever write `out1`.
   `out0` (`[sp+0]`) is unread in the handler we disassembled, but the framework may be expected to
   fill it, and something else may consume it.
2. **The frame vector's context.** `--ctx` hands the game two zeroed scratch buffers where RetailOS
   passes real structures. The poll handler branches on `ldrb r0, [r5, #0x0]` — a byte in that
   context — and takes the `eq` path because our buffer is zero. The non-zero path is unexplored
   and is where a button mode could live.

Measured as no-ops, for the record: all 256 low-byte codes held continuously; the same 64 codes as
press/release bursts; a wheel-position ramp; intermittent touch/release gestures with advancing
position; and forcing `Audio #0/#40/#42/#43/#45/#48/#51/#55/#56`, `miscTBD #6/#12/#13/#14`,
`Settings #0` and seven `OpenGLES` entries to non-zero.

`miscTBD #12` (`0x00268570`) is worth noting while in there: it fills a struct with five byte
fields plus a halfword — a date/time getter (sec/min/hour/day/month/year). It is reached and
unstubbed, which is why the clock on screen never moves.

## 6. Instruments added to `eapp-loader`

All opt-in; defaults preserve the behaviour every other title was measured against, and the full
suite (119 tests) passes.

| flag | what it does |
|---|---|
| `--open-returns-handle` | `FileOpen` returns the handle instead of 0 |
| `--async-files` | `AsyncFileIO #0/#3` open, `#2` read, completions drained per frame |
| `--poke-at=FRAME:ADDR=VAL` | write a byte between two frames |
| `--call-at=FRAME:ADDR:A0,A1` | call a guest function between two frames |
| `--dump=ADDR:N` | hexdump guest memory after the run |

`play` gains `--async-files` and a keyboard→`InputEvents` mapping.

## 8. Input, solved — and it was never the event list

The buttons are **bits in the game's input flags word**, not entries in the event queue. The
consumer at `0x180082c8` snapshots the input block and tests them one at a time:

```asm
180082d8  ldr r4, [sp, #0x4]   ; the flags word
18008304  and r2, r4, #0x4
18008318  and r2, r4, #0x8
1800832c  and r2, r4, #0x2
18008340  and r2, r4, #0x1
18008354  and r2, r4, #0x10
18008390  tst r4, #0x20        ; the wheel's own "event present"
```

Five bits for five click-wheel buttons, plus `0x20` for the wheel. Set a bit in `[r9+0x14]`
(`0x18037a0c`, r9 from the literal at `0x18018b48`) and the game sees a button. `0x10` is **Menu** —
measured: it opens the pause menu, which then renders correctly over the course.

**The gate that hid all of this** is at `0x18018a44`: the game only reads its input at all on
frames whose flags are non-zero, and only an `InputEvents #0` poll reporting an event sets them.
A button pressed while the wheel is still is never looked at. That is why Select appeared to work
(you are usually scrolling when you press it) and every other button appeared dead. A wheel sample
must accompany every button, and one must be in flight each frame or a menu cannot be driven.

## 9. Audio, as far as it is read

Not implemented — but its shape is now known, and it is **not** the same kind of API as the others.

| entry | osos | what it is |
|---|---|---|
| `#0` | `0x0026a20c` | create: `alloc(0x4c)`, installs a vtable, defaults `0xac44` = **44100 Hz** |
| `#2` | `0x0026a534` | look up a sound by handle through the singleton at `[0x10800024]` |
| `#13` | `0x0026a7cc` | setter — writes the argument to `object+0x1c` |
| `#14` | `0x0026a718` | setter — writes to `object+0x24` |
| `#15` | `0x0026a73c` | setter, same shape |
| `#42` | `0x002687fc` | posts message **`0x66000010`** |
| `#45` | `0x00268b48` | posts message **`0x66000015`** |
| `#52` | `0x00268890` | **`mov r0,#0xff; bx lr`** — returns a constant 255 |

`#52` is worth calling out: research/01 found it by sweeping constants and settled on 1, which
avoids the divide-by-zero it causes. Apple's implementation returns **255**, and that is now what
the stub returns.

The `0x66xxxxxx` values are the same message family `InputEvents #1` decodes (`0x66000009`…`b`),
so audio is a **message-passing command queue**: allocate a 12-byte message, stamp an id, post it.
Playing a sound is therefore not a call to intercept but a protocol to implement, on top of a
decoder and a mixer that do not exist here yet. That is the honest size of the job.

### Audio, further: the assets are mapped, the trigger is not

Static scan of the game's own BL sites: it calls **18 of the 61** Audio entries. Runtime reaches
14, and the two sets differ, so several are dispatched indirectly.

Registration is fully understood:

| call | what it registers |
|---|---|
| `#0` ×10 | the sound effects — index 0..9, stride `0x21` in a table at `0x18040ee8`, one per `c00bank/0-9.wav` |
| `#40` ×6 | the music — `(pointer, duration_ms)` pairs from a loop at `0x180048dc`; `0x1a9c8` = 109 000 ms for a 1.8 MB AAC track, which is right |
| `#55` | `0x7fff` — 16-bit max volume |
| `#13/#14/#15` | per-frame parameter refresh on handle 0, almost certainly the music channel |

**The sound-effect play path is `0x18017d3c`** and reads clearly:

```asm
18017d84  bl #10                  ; begin/reset the voice
18017d94  bl #12  r1=[r5+0x56]
18017da0  bl #11  r1=[r5+0x4a]
18017dac  bl #8   r1=[r4+0x15c]   ; the sound id
18017dd8  bl 0x18000b64           ; allocate a buffer
18017df0  bl #7   r1=<buffer>
18017df8  bl #23                  ; -> handle stored at [r4+0x118]
```

**None of `#7`–`#23` has ever fired**, in any session, including gameplay with sounds that should
be audible. So the game is deciding not to play rather than playing into a void. The gate is at
`0x18017d6c`: `ldr r0,[r4,#0x124] / cmp r0,r2 / bne` skips the whole path unless a field matches an
argument. `0x18017d3c` has exactly one caller (`0x18017d30`), which itself has none — it is reached
through a function pointer, so the condition has to be caught at runtime rather than read statically.

Ruled out: it is not a settings gate. The only `Settings #0` query the game makes is for
`"Language"`, and no sound/volume/mute setting string exists in the image.

**What playback would still need**, beyond finding that gate: intercepting the `0x66xxxxxx` message
queue, a WAV decoder for the effects and an AAC decoder for the music, a mixer, and a host output
device. None of that exists here. The effects are the tractable half — ten short WAVs already
loaded into memory, with a known index.

### The sound-effect gate, measured — and what it ruled out

Watching `0x18017d70` (the `cmp`, not the `ldr` before it — the enter-log samples registers
*before* the instruction, so watching the load reports the stale `r0`) during play:

```
r0 = 0x2c    ; [r4+0x124], this voice's id
r2 = 0x00    ; the id being asked for
```

Seven arrivals, seven branches to the skip at `0x18017e60` — **and one arrival at `0x18017d78`**,
the success path. So the configure-and-play sequence does run. The game is not refusing to play,
which retires the "something upstream is gating audio" theory.

`r3` at that point steps `0x84, 0xa5, 0xc6, 0xe7, 0x108, 0x129` — increments of `0x21`, the sound
table's entry stride, confirming this function walks the ten registered effects.

**But `Audio #8` is never called during gameplay.** Hooking it (Apple's implementation is
`str r4,[obj+0x08]`, so its argument is the sound id) and playing a full hole captured **zero**
ids, while the game ran hard the whole time — ~91 000 instructions and ~9 quads per frame. So
`0x18017d3c` is not the routine effect trigger; whatever fires a putt or a bounce goes somewhere
else, most likely straight through the `0x66xxxxxx` message queue without touching a voice object.

Where that leaves audio: the assets, the registration, the object layout and the volume scale are
all mapped, and the game demonstrably wants to make sound. The missing pieces are the trigger and
the entire output half — decode, mix, device. `Stub::SfxId` and the `afplay` hook in `play` are
left in place; they are correct as far as they go and simply never fire yet.

### 9a. Audio, solved: `Audio #2` is the trigger, and it was in the call list all along

Everything above from "the sound-effect play path is `0x18017d3c`" onward chased the wrong
function. `0x18017d3c` is a configure path that the game runs rarely; it is not how a putt makes a
noise. The trigger is **`Audio #2`**, which had been in the reached-at-runtime list since the very
first gameplay trace and was read as "look up a sound by handle" because its first two instructions
are exactly that. The tail is the part that matters:

```asm
0026a534  mov r1, r0              ; r1 = handle, the only argument
0026a540  ldr r0, [0x10800024]    ; the descriptor table
0026a544  bl  0x0029cbc4          ; table_at(table, handle) -> SoundEffectDescriptor*
0026a54c  bl  0x001b9084          ; the SFX engine singleton
0026a560  b   0x001b9168          ; Play(engine, desc, callback=0, ctx=0)
```

`Audio #8` was a red herring twice over: it is a plain field setter (`desc+0x08 = buffer length`),
which is why hooking it captured zero ids across a whole hole. `Stub::SfxId` has been deleted.

**The lesson worth keeping:** the trigger was never missing from any measurement. It sat in
`[0, 2, 13, 14, 15, 40, ...]` in every census printed for weeks. The search kept looking for a call
that *wasn't* firing, when the answer was a call that was firing constantly and had been mislabelled.

#### The object

`Audio #0`'s vtable is `0x006710e8`; the typeinfo one word earlier names the class from RTTI:
**`SoundEffectDescriptor`** (string at `0x00666014`), `0x4c` bytes. `#0` mallocs one, fills defaults
(44100 Hz, mono, 16-bit, volume `0x7fff`, pitch 1000) and returns the **0-based slot index** it was
inserted at. Ordinals `#7`–`#22` are its setters and `#23`–`#39` its getters, one field each:
`+0x04` PCM pointer, `+0x08` byte length, `+0x10` rate, `+0x14` channels, `+0x18` bits, `+0x1c`
volume, `+0x20` pitch, `+0x24` pan, `+0x3d` state, `+0x44`/`+0x48` the duck target and ramp.

#### Which sound is which — not from the arguments

Minigolf **never calls `#7`**, so RetailOS is not where the PCM comes from, and `#0`'s arguments do
not describe the sound either: `r2`/`r3` point at `0x18040ee8 + index*0x21`, which reads back
`0xff`-filled at call time — it is the game's own handle table, an output.

What `#0` does carry is `r1` = 0..9 across ten calls at course load, and each course ships exactly
ten sounds as `cNNbank/0.wav` … `cNNbank/9.wav`. The game's own format string at `0x47b0` is
`c%02dbank/%01d.wav`, so the mapping is one-to-one and stated by the game rather than inferred.
The course is tracked from the asset file names (`c00`, `c000`, `c00.en` → bank `c00bank`), because
`c01bank` and `c02bank` hold different sounds under the same indices.

#### The four-voice pool is load-bearing

`0x00217a70` loops `cmp r4,#4`: the mixer has exactly **four** voices, and when all are busy
Apple's `Play` returns at `0x001b91a8` without even setting the descriptor's state byte — a
completely silent drop. This is not a detail to skip. One hole of golf issues **145** `#2` calls,
of which only 19 become sounds once the pool limit and one-voice-per-descriptor are honoured.
A port that ignores the limit plays a hole roughly eight times noisier than the device does.

Also measured: `0x00145f90` snapshots volume/pan/pitch into the voice **at Play time only**, so the
per-frame `#13`/`#14`/`#15` refresh does not affect a voice already sounding.

#### Music is a different subsystem entirely

`#40`/`#43` do not touch the mixer at all: they allocate a 12-byte message, stamp an id
(`0x6600000d`, `0x66000011`) and post it to the iPod's own player task at `0x0012d930`. Ordinals
40–46, 48, 50, 53 and 59 are all that same shape. Effects and music share no code below the API,
which is why finding one told us nothing about the other.

## 10. The status bar: `miscTBD #12` is the clock, `#13` is the battery

Minigolf draws the iPod's own status bar itself — clock at the top left, battery gauge at the top
right — and both come from `miscTBD`, not from `Settings` and not from the frame context. Neither
was stubbed, so the clock formatted whatever was on the stack (a stable-looking "1:17") and the
gauge read empty.

**Neither reference is findable with a literal-pool scan.** The game reaches its strings with
`add rN, pc, #imm`, so searching for the word `0x1800ecc4` returns nothing at all — which is what
made the clock look like it had no source in the binary. Scanning for PC-relative forms
(`e28f_____`/`e24f_____`, target = `pc + 8 ± rotated_imm`) finds both immediately:

| code | string |
|---|---|
| `0x1800eb78` | `"% 2d:%02d"` at `0x1800ecc4` — the clock |
| `0x18004750` | `"c%02dbank/%01d.wav"` at `0x180047b0` — the sound bank |

### The clock — `miscTBD #12(struct *out)`

```asm
1800eb6c  bl 0x18012c00     ; a veneer: b 0x1800099c, which is the miscTBD #12 thunk
1800eb70  ldr r2,[sp,#36]   ; struct +8  -> first  %d, the hour
1800eb74  ldr r3,[sp,#32]   ; struct +4  -> second %d, the minute
1800eb78  add r1,pc,#324    ; "% 2d:%02d"
1800eb80  bl <sprintf>
```

The struct occupies `sp+28..sp+52`, i.e. six words. `+4` = minute and `+8` = hour are measured;
the rest are filled in the usual `tm` order (second, minute, hour, day, month, year), which those
two offsets agree with and which this game never reads. The hour is 12-hour — the format carries
no AM/PM, and that is how the device shows it.

### The battery — `miscTBD #13()`, on a 0..20 scale

`0x180140cc` wraps it in a cache keyed on `miscTBD #9`, then decodes it:

```asm
18014134  bl <miscTBD #13>  ; only when the clock says the sample is stale
1801414c  cmp r0,#20
18014150  movhi r0,#20      ; the raw range is 0..20, not 0..100
18014160  mul r0,r5,#100
18014164  bl <divide by 20> ; -> percent
```

and the caller turns that percent into one of eleven icon frames:

```asm
1800ebf4  bl 0x180141a8     ; -> percent
1800ebf8  cmp r0,#100
1800ebfc  movgt r0,#100
1800ec04  bl <divide by 10> ; frame index 0..10
```

**Returning a percentage from `#13` would peg the gauge full at anything above 20%.** The stub
returns `(percent * 20 + 50) / 100`.

Verified by screenshot: `--battery=100` draws a full green cell, `--battery=5` an empty one, and
the clock reads the host's wall time to the minute.

### Why the emulator's other battery work does not cover this

`Pcf50605::set_battery_percent`/`set_clock` feed the **PMU model**, which only exists on the
full-system boot path and needs a NOR dump. A game running as an eApp never touches the PMU: it
asks `miscTBD`, and RetailOS answers. The two paths are independent and both are now wired to the
host.

### 10a. `Audio #48` is the repeat mode, and the device is what loops a track

Traced end to end, and it explains why background music stopped after one track length.

`#48` posts message `0x66000017`. The message router at `0x0012d930` is **not** the handler — it
only keeps bookkeeping and enqueues. The real dispatcher is the player task loop at `0x0012e58c`
(`sub r1,#13 / sub r1,#0x66000000 / cmp r1,#12 / addls pc,pc,r1,lsl #2`, table at `0x0012e608`).
Index 4 (`0x11`) is the confirmed play-by-index arm, which anchors the table; index 10 (`0x17`)
routes to `0x00268d68`, which tail-calls `setRepeatMode` at `0x000b3908`.

`setRepeatMode` remaps the public value — 0 stays 0, **1 and 2 swap** — and stores it as a halfword
at `engine+0xE4`. Two readers fix what that field means:

* skip-forward `0x0009dc54` leaves the track index **unchanged** for internal 2 and wraps it to 0
  past the end for internal 1. So internal 2 = "one", internal 1 = "all".
* end-of-item `0x000c9610` re-queues the finished item **only when the mode is not off**:
  `ldrh r0,[r0,#0xe4] / cmp r0,#0 / ldrne r0,[r4,#0xcc] / beq <don't requeue>`.

After the swap the public order is **0 = off, 1 = one, 2 = all** — the order of the iPod's own
Settings > Repeat menu. Minigolf sets 1.

**This is a behaviour a host must supply.** The device restarts the stream itself, so a game sets
the mode once and never issues another play; an emulator that plays the file once and stops goes
quiet after one track length (45 s for `m0.m4a`) with nothing in any log to explain it.

`Audio #50` (`0x66000016`) is the matching **shuffle** setter — `0x000b3a04` writes `prefs+0x20`
and `prefs+0x92` and regenerates the play order — and `#47`/`#49` are the two getters. RetailOS
treats both as settings a game may clobber: `0x000f5904` restores them from a saved byte pair when
a game releases audio, keyed on a flag the Audio wrappers set at `0x00268bb0`.

## 11. Metadata

Read statically out of `osos.bin` only — nothing here has been run. Every address is a file offset.
Where a name is guessed rather than measured it says so.

### 11.1 What it is: the iPod's own music/photo library, exposed to games

Not a guess — the classes name themselves. The 152 shims at `0x00268df0`–`0x0026a208` all funnel
into a small set of C++ classes, and almost every one of them has a live RTTI record:

| typeinfo | name string | mangled name |
|---|---|---|
| `0x00665e0c` | `0x00665f28` | `12SMusicFilter` |
| `0x00665e6c` | `0x00665f77` | `13SMusicLibrary` |
| `0x00665e74` | `0x00665f87` | `13SPhotoLibrary` |
| `0x00665e7c` | `0x00665f97` | `15SArtworkLibrary` (derives from `SPhotoLibrary` — its `__si_class_type_info` base word at `0x00665e84` is `0x00665e74`) |
| `0x00665e88` | `0x00665fa9` | `15TCountedPointerI10SImageDataE` |
| `0x00665eb8` | `0x0066602c` | `6SImage` |
| `0x00665ec0` | `0x00666034` | `6STrack` |
| `0x00665ee0` | `0x00666045` | `9SPlaylist` |

So: **`Metadata` is a handle-based wrapper over the iTunesDB — tracks, playlists, browse
categories — plus the photo/album-art image store.** It is exactly the API the iPod's own music
menu is built on, re-published for eApps.

### 11.2 The registry: seven `Tracker<T>` handle tables

Every shim starts by fetching one singleton. `0x0013847c` is a guarded accessor over the global at
VA `0x10800028`; on first call it `malloc(0xa8)` and constructs **seven containers in a row**, each
`0x18` bytes:

```asm
00138490  mov r0, #0xa8
00138494  bl  0x25a974            ; malloc
00138498  mov r3,#2 / mov r2,#10 / mov r1,#0
1384a4  bl  0x29c3d0             ; +0x00
1384b4  add r0, r0, #0x18 / bl 0x29c638   ; +0x18
1384c8  add r0, r0, #0x18 / bl 0x29c170   ; +0x30
1384dc  add r0, r0, #0x18 / bl 0x29d4cc   ; +0x48
1384f0  add r0, r0, #0x18 / bl 0x29cfdc   ; +0x60
138504  add r0, r0, #0x18 / bl 0x29cd7c   ; +0x78
138518  add r0, r0, #0x18 / bl 0x29c898   ; +0x90
138520  sub r0, r0, #0x90 / str r0,[r4]
```

The container class names itself too: each instantiation ends in the debug format string
**`"Tracker<%s> fTable=%x, fSize=%d"`**, and the word immediately *before* that string is the
element's typeinfo pointer. That gives the table→class mapping with no guessing:

| registry offset | element | `Tracker<%s>` string | element typeinfo | handle→object accessor |
|---|---|---|---|---|
| `+0x00` | `SMusicLibrary` | `0x0029c474` | `0x00665e6c` | `0x00120d90` |
| `+0x18` | `SArtworkLibrary` | `0x0029c6dc` | `0x00665e7c` | *(inline in the shims)* |
| `+0x30` | `SMusicFilter` | `0x0029c214` | `0x00665e0c` | `0x00120d64` |
| `+0x48` | `SPlaylist` | `0x0029d570` | `0x00665ee0` | `0x0011e37c` |
| `+0x60` | `STrack` | `0x0029d080` | `0x00665ec0` | `0x0011e3a8` |
| `+0x78` | `SImage` | `0x0029ce20` | `0x00665eb8` | `0x0011e350` |
| `+0x90` | `TCountedPointer<SImageData>` | `0x0029c93c` | `0x00665e88` | `0x000bfd70` (+ `0x0029e0c8` to deref the counted pointer) |

(The image carries ten `Tracker<%s>` instantiations; the two extra, `iFSFile` at `0x0029d308` and
`eAppAsyncFileSupport::DirectoryList` at `0x0029d7d0`, belong to `Filesytem`/`AsyncFileIO`, not
here.)

**A handle is a 0-based array index, and `0` is a legal handle.** All six accessors are the same
five instructions — this is `0x00120d90`:

```asm
00120d98  bl   0x13847c
00120d9c  cmp  r4, #0
00120da0  ldrge r1, [r0, #4]      ; fSize
00120da4  cmpge r1, r4
00120da8  ldrgt r0, [r0]          ; fTable
00120dac  movle r0, #0            ; out of range -> NULL object
00120db0  ldrgt r0, [r0, r4, lsl #2]
```

`Add` (`0x0029c238` and its per-type twins) scans for the pointer already being tracked and
returns **`-1`** if so, otherwise grows the table and returns the index of the first NULL slot.
`Remove` (`0x0029c384` etc.) bounds-checks, runs the destructor, frees, NULLs the slot and
decrements the live count. **So `-1` is the failure value, not `0`** — the same trap section 3
recorded for `AsyncFileIO`.

### 11.3 Strings cross the ABI as `(buf, int* len)`

Everything that returns text goes through `0x0011c708`:

```asm
0011c718  bl 0x256610          ; SString::c_str()  ([this+4], or the empty string at 0x2a9c1c)
0011c730  ldr r2, [r4]         ; *len = capacity IN
0011c738  bl 0x7ce4c           ; strncpy(buf, str, capacity)
0011c744  bl 0x284318          ; strlen  (byte loop -> the ABI string is 8-bit, NUL-terminated)
0011c748  str r0, [r4]         ; *len = actual length OUT
```

So the exported signature of every string getter is `f(handle, char* buf, int* len)` where `*len`
is the buffer capacity going in and `strlen(result)` coming out. The internal `SString` is built on
the stack and destroyed with `0x002247bc` before the shim returns.

### 11.4 `STrack` — measured against the iTunesDB parser

`STrack` is 20 bytes; its constructor is `0x0021e408`:

| offset | field | evidence |
|---|---|---|
| `+0x00` | vtable, `0x00676608` (typeinfo `0x00665ec0` at `0x00676604`) | |
| `+0x04` | pointer to the in-RAM track record | `str r1,[r0,#4]` |
| `+0x08` | the music database | `[global][+0x1c]` |
| `+0x0c` | database generation | `db[0xcf8][0x10]` |
| `+0x10` | kind byte | `strb r2,[r0,#0x10]` |

`vt+0x00` (**ordinal 63**) is `IsValid` — `0x00255c60` checks the record is still live *and*
`db[0xcf8][0x10] == this->generation`, i.e. **a track handle goes stale when the database
reloads**. Every other `STrack` virtual calls `vt+0x00` first and returns 0 / the empty string when
it is false. A host that never invalidates can hard-code it to 1.

The record at `[track+4]` is the parsed `mhit`. The parser is at `0x000d7300`–`0x000d826c`
(`'mhit'` literal at `0x000d8274`, `'mhod'` at `0x000d8284`), it reads the on-disk `mhit` into a
stack buffer at `sp+0x424`, so **`mhit` offset `F` is `sp+0x424+F`** and the stores name the record
fields outright:

```asm
000d7a44  ldr r0,[sp,#0x44c] / str r0,[r4,#0x58]   ; mhit+0x28 tracklen(ms) -> rec+0x58
000d7a4c  strh r0,[r4,#0x80]                        ; mhit+0x2c track_nr    -> rec+0x80
000d7a5c  strh r0,[r4,#0x84]                        ; mhit+0x5c cd_nr       -> rec+0x84
000d7a88  strh r0,[r4,#0x76]                        ; mhit+0x34 year        -> rec+0x76
000d7a90  strh r0,[r4,#0x74]                        ; mhit+0x38 bitrate     -> rec+0x74
000d7ab8  strb r0,[r4,#0x7a]                        ; mhit+0x1f rating      -> rec+0x7a
000d7b38  add r5,r4,#0x108 / stm r5,{r0,r3}         ; mhit+0x70 dbid (8 B)  -> rec+0x108
000d7d9c  ldreq r0,[r4,#0x1c] / orreq r0,r0,#0x40   ; mhit+0xa4 has_artwork -> rec+0x1c bit 6
```

The string fields go through a per-category string pool. Each getter is three instructions, e.g.
`0x0009fd78`:

```asm
0009fd7c  ldr r1, [r0, #0x28]      ; the record's index into the pool
0009fd80  ldr r0, [r0]             ; the database
0009fd84  add r0, r0, #0xb4        ; pool #1
0009fd88  b   0x9f0c4              ; pool lookup -> SString
```

and the `mhod` dispatch in the parser (jump table at `0x000d7e48`, index = `mhod` type) says which
pool is which — this is the decisive evidence for the string names:

| `mhod` type | handler | record field | pool | meaning |
|---:|---|---|---|---|
| 1 | `0xd7ee4` | `rec+0x28` | `db+0xb4` | title |
| 2 | `0xd7ec8` | *(special, `0x1179bc`)* | — | location / file path |
| 3 | `0xd7f14` | `rec+0x34` | `db+0x138` | album |
| 4 | `0xd7f58` | `rec+0x2c` | `db+0xe0` | artist |
| 5 | `0xd7fa0` | `rec+0x40` | `db+0x190` | genre |
| 6 | `0xd7fd0` | `rec+0x44` | `db+0x1bc` | filetype / "Kind" |
| 7 | `0xd7fe4` | `rec+0x48` | `db+0x1e8` | EQ preset |
| 8 | `0xd8078` | `rec+0x4c` | `db+0x214` | comment |
| 9 | `0xd808c` | `rec+0x50` | `db+0x240` | category |
| 12 | `0xd7f88` | `rec+0x3c` | `db+0xe0` | composer (shares the artist pool) |
| 13 | `0xd7f44` | `rec+0x38` | `db+0x164` | grouping |
| 14 | `0xd80a0` | `[rec+0x10]+0x08` | `db+0x424` | description |
| 16 | `0xd80c8` | `[rec+0x10]+0x04` | `db+0x450` | podcast URL |
| 17 | `0xd8114` | chapter data | — | |
| 19 | `0xd8194` | `[rec+0x10]+0x0c` | `db+0x47c` | TV show |
| 20 | `0xd81b8` | `[rec+0x10]+0x10` | `db+0x4a8` | TV episode |

`0x00676608` is the vtable that ties it together. **Ordinals 63–107 walk it in slot order**
(`+0x00, +0x04, +0x08, …`) with only the late additions out of place, which means the ordinals
follow C++ declaration order.

> ⚠️ **The house "ordinals are alphabetical" rule does not hold for `Metadata`.** Ordinals 66–75
> are Title, Album, Artist, Genre, Composer, Grouping, Kind, EQ, Comment, Category — declaration
> order, not alphabetical order. And the whole framework is grouped by class in the order
> lifecycle, `SImage`, `SImageData`, `SMusicLibrary`, now-playing playlist, `STrack`,
> playlist-from-library, `SPlaylist`, now-playing playlist again, which is not alphabetical either.
> Nor are the library counts (`#40` album, `#41` artist, `#42` genre, `#43` playlist, `#44`
> composer, `#45` song — composer is in the wrong place for that to be a sort). Do not use the rule
> to cross-check a `Metadata` naming; it will mislead.

> A second warning of the same kind: ordinals **141–151** sit after the last "normal" block and are
> a **later addition to the interface** — two `SMusicLibrary` entries and nine `STrack` entries,
> appended rather than inserted so the existing ordinals kept their numbers. `#143` in particular
> is a string getter that belongs with `#66`–`#77`.

### 11.5 The 152 ordinals

Confidence key: **H** = the behaviour is read directly out of the disassembly and the name follows
from a named artefact (RTTI, `mhod` type, iTunesDB field). **M** = behaviour measured, name
inferred. **L** = mechanism measured, meaning not established.

#### Lifecycle (create / destroy), `Tracker` operations

| # | addr | name / behaviour | args → return | conf |
|--:|---|---|---|:--:|
| 0 | `0x2695c4` | `MusicLibraryCreate` — `malloc(8)`, `SMusicLibrary::ctor(obj, kind=0, source=0)` at `0x1678b4`, `Tracker<SMusicLibrary>::Add` | `()` → library handle, `-1` on failure | H |
| 1 | `0x2695ac` | `MusicLibraryDestroy` — `Tracker<SMusicLibrary>::Remove` (`0x29c384`) | `(lib)` → void | H |
| 2 | `0x268e1c` | `ArtworkLibraryCreate` — `malloc(12)`, `0x18d098(obj,0)`, `Tracker<SArtworkLibrary>::Add` | `()` → artwork-library handle | H |
| 3 | `0x268e00` | `ArtworkLibraryDestroy` (`0x29c5e4`) | `(alib)` → void | H |
| 4 | `0x269304` | image from the artwork library — `0x18d010(&tmp, artworkLib, a2, a3)`, `Tracker<SImage>::Add`. **`r1` is loaded over and discarded** | `(alib, ignored, a2, a3)` → image handle | M |
| 5 | `0x268eec` | `ImageDestroy` — `Tracker<SImage>::Remove` (`0x29cd30`) | `(image)` → void | H |
| 13 | `0x268f08` | `ImageDataRelease` — `Tracker<TCountedPointer<SImageData>>::Remove` (`0x29c84c`) | `(imagedata)` → void | H |
| 28 | `0x268e78` | `MusicFilterCreate(name)` — wraps the C string in an `SString`, then `0xc9220(name, -1)` = `malloc(16)`, `SMusicFilter::ctor(obj, name, id)` at `0x148dac`, `Tracker<SMusicFilter>::Add` | `(const char* name)` → filter handle | H |
| 60 | `0x268f5c` | `TrackRelease` — `Tracker<STrack>::Remove` (`0x29cf90`) | `(track)` → void | H |
| 61 | `0x268f24` | `MusicFilterRelease` — `Tracker<SMusicFilter>::Remove` (`0x29c124`) | `(filter)` → void | H |
| 110 | `0x268f40` | `PlaylistRelease` — `Tracker<SPlaylist>::Remove` (`0x29d478`) | `(playlist)` → void | H |
| 118 | `0x268ea4` | `MusicFilterGetName` — `0x148da4(&s, filter)` then `0x11c708` | `(filter, char* buf, int* len)` → void | H |

#### `SArtworkLibrary` / `SImage` / `SImageData` — album art

Layouts, all from their constructors:

* **`SArtworkLibrary`** (`0x0018d098`, 12 bytes): `{ vptr = 0x0066bfd8, artworkDB = [[0x1081da18]+0x1c]+0xd2c, u8 lockMode +8 }`
* **`SImage`** (`0x0021be3c`, 8 bytes): `{ dbImageRecord +0, u8 lockMode +4 }`
* **`SImageData`** (built inline at `0x0021bcdc`/`0x0021bdd8`, 12 bytes, held in a `TCountedPointer`): `{ loadedBitmap* +0, dbImageItem* +4, u16 formatId +8, u8 lockMode +0xa }`

| # | addr | impl | args → return | conf |
|--:|---|---|---|:--:|
| 4 | `0x269304` | `0x18d010(&tmp, alib, id_lo, id_hi)` → `Tracker<SImage>::Add`. **The skipped `r1` is EABI 64-bit alignment padding, not a discarded argument** | `(alib, uint64 songPersistentID)` → image handle | H |
| 6 | `0x26958c` | `0x21be34` → `0xa132c` — `[rec+8]` exists and `[[rec+8]] == 0x6974696c` | `(image)` → bool `IsValid` | H |
| 7 | `0x269554` | `0x21bd38` → `0x9b214` — `[rec+0x0c]` | `(image)` → image ID | H |
| 8 | `0x269390` | `0x21bbb4` → `0x93a1c` — counts image-data items with `[item+8] == formatId` | `(image, s16 formatId)` → count | H |
| 9 | `0x269534` | `0x21bbbc` — `0xe4d28([[rec+0x28]], buf, 1)` | `(void* out10, image)` → broken-down **date** | H |
| 10 | `0x269564` | `0x21be4c` — `[recA+0x0c] == [recB+0x0c]`; both NULL → true, one NULL → false | `(imageA, imageB)` → bool | H |
| 11 | `0x2693bc` | `0x21bd64` — walks `0x9db08` `index+1` times | `(image, s16 formatId, bool load, u32 index)` → imagedata handle | H |
| 12 | `0x269454` | `0x21bc58` — keeps the **last** item whose byte size `[item+0x0c]` is ≤ `maxSize` | `(image, s16 formatId, bool load, u32 maxSize)` → imagedata handle | H |
| 14 | `0x269520` | `0x12c8d0` — **loads the bitmap**: builds the `.ithmb` path, `0x11ca20` allocates the surface, seeks `[item+0x18]`, reads through `[item+0x1c]`, fills w/h from `0x167b38`, stamps magic `0x565`, stores at `[this+0]`. No-op if already loaded | `(imagedata)` → void | H |
| 15 | `0x2694c4` | `0x12ca98` → `0x9b214` — `[item+0x0c]` | `(imagedata)` → **byte size** | H |
| 16 | `0x269440` | `0x12ca58` — the *same* `[item+0x0c]`, inlined with its own null check | `(imagedata)` → byte size | H |
| 17 | `0x26942c` | `0x12cb2c` — `[this+0]` | `(imagedata)` → loaded bitmap pointer, NULL if not loaded | H |
| 18 | `0x2694d8` | `0x12cac4` — `0x9b1b4` fills the file name, `0x9b164` writes `[item+0x18]`/`[item+0x1c]` | `(imagedata, SString* outName, void* outOffset, void* outRef)` → bool | H |

Worth stating plainly because it is the obvious wrong guess: **neither `#15` nor `#16` is a width or
a height** — both return the byte length at `[item+0x0c]`. The pixel dimensions live in the four
shorts at `[item+0x10..0x16]` (readable through the *unexported* `0x0012cb50`) and in the header
`0x0012c8d0` writes into the loaded surface. `#11`/`#12` are the two ways to pick a size variant:
by index, or by "largest that fits in N bytes".

#### `SMusicLibrary` (`0x00120d90`) — ordinals 19–59, 141, 142

Every one of these has the same preamble, which is worth knowing before stubbing anything:

```asm
00165a70  push {r2,r3,r4,lr}
00165a74  mov  r4, r0
00165a7c  bl   0x17ca6c            ; enter scope / take the DB lock
00165a80  ldrb r0, [r4, #4]        ; the library's "kind" byte
00165a84  cmp  r0, #2
00165a88  bne  0x165a9c
00165a90  bl   0x17ca8c            ; kind 2 -> release and return 0 immediately
00165a94  mov  r0, #0
00165a9c  cmp  r0, #1
00165aa4  bleq 0x17ca44            ; kind 1 -> a second lock
```

`SMusicLibrary` is 8 bytes: `{ +0x00 database*, +0x04 kind, +0x05 source }`, built by
`0x001678b4(this, kind, source)`. With `source == 0` the database pointer is taken from
`*(global)[+0x1c]` — the main on-device library; `source == 1` takes a second global; anything else
clones one through `0x000a2410`. Ordinal 0 always passes `(0, 0)`.

The `[lib+0]` object is the **browser context** over the music database (`0x0009bc6c` maps it to
the root: `if (*(u8*)p == 1) return p; if (== 2) return p->0xc9c; else 0`). It caches **seven
browse-category lists**, each `{names[], sortNames[], count}` plus a selection index and a 512-byte
name buffer:

| cat | names | sort | count | sel | name buf | build mask (`0xd080c`) | invalidator |
|---|---|---|---|---|---|---|---|
| A | `+0xc60` (+`0xc68` artwork) | `+0xc64` | `+0xc6c` | `+0x18` | `+0x628` | `2` / `0x80000000` | `0xa3450` |
| B | `+0xc48` | `+0xc4c` | `+0xc50` | `+0x10` | `+0x228` | `4` | `0xa34b8` |
| C | `+0xc54` | `+0xc58` | `+0xc5c` | `+0x14` | `+0x428` | `0x4000000` | `0xa3504` |
| D | `+0xc3c` | `+0xc40` | `+0xc44` | `+0x0c` | `+0x28` | `0x40` | `0xa3620` |
| G | `+0xc70` | — | `+0xc74` | `+0x1c` | `+0x828` | hi `0x8000` | `0xa36b4` |
| E | `+0xc78` | — | `+0xc7c` | `+0x20` | `+0xa28` | hi `0x10000` | `0xa3730` |
| F | `+0xc80` (**ints**) | — | `+0xc84` | `+0x24` | `+0xc28` | hi `0x20000` | `0xa36fc` |
| tracks | `+0xc88` | `+0xc8c` | `+0xc90` | — | — | built by `0xcc7a8` | `0xa376c` |

plus playlists `+0xcfc[]` / count `+0xd00`, a second playlist list `+0xd04[]` / count `+0xd08`,
`+0xc34` = current browse mode 0..9, `+0xcf8` = the string-pool chunk, `+0xcf0` = current playlist.
The invalidation cascade proves the hierarchy **D → {B, C} → A → G → tracks** and, separately,
**E → F → tracks**.

**Which category is which** — settled by joining each name-getter's pool offset to the `mhod` table
in 11.4, and by the iPod's own UI:

| cat | name-getter pool | `mhod` field | identity | conf |
|---|---|---|---|:--:|
| B | `chunk+0xe0` | 4 (`rec+0x2c`) | **Artist** — sort variant at `rec+0x30`, locale-gated at `0x1ecb7c` | H |
| A | `chunk+0x138` | 3 (`rec+0x34`) | **Album** — the only category with a third parallel array (`+0xc68`), and that array feeds artwork | H |
| D | `chunk+0x190` | 5 (`rec+0x40`) | **Genre** — top of the music hierarchy | H |
| C | `chunk+0xe0` | 12 (`rec+0x3c`) | **Composer** — shares the artist pool; `mhod` 12 is composer and Composers is a real 5G browse menu. (Album Artist, `mhod` 22, is the alternative reading; it is *not* a 5G menu item and its record field is not `rec+0x3c`.) | M |
| E | `chunk+0x47c` | 19 | **TV Show** | H |
| F | *(returns an `int`)* | — | **TV season number** | H |
| G | read from the *current playlist* (`0x9bb98(db)+0xb4`, gated on `playlist+0xed & 0x80`) | — | a playlist-scoped list | L |

E and F are nailed by the UI: `0x0024555c` sets mode 7 and lists shows, `0x00245268` sets mode 8,
selects a show and lists its seasons, `0x002451ac` sets mode 9 and lists that season's episodes.

##### (i) Set the browse mode

Nine identical bodies; only one immediate differs, and all of them end in `0xb4038`
(`str r1,[r0,#0xc34]; bx lr`). `0x000a00d0` reads `+0xc34` back through a 10-entry jump table at
`0x000a00d8` to choose the track sort field.

| # | addr | mode | sort field | selects |
|--:|---|--:|--:|---|
| 48 | `0x269a04` | 0 | 9 | Genre |
| 46 | `0x2699d4` | 1 | 7 | Artist |
| 50 | `0x2699e4` | 2 | 24 | Composer |
| 47 | `0x2699c4` | 3 | 6 | Album |
| 51 | `0x269a14` | 4 | 5 | cat G |
| 52 | `0x269a24` | 5 | 1 | Songs (title sort) |
| 20 | `0x269a44` | 7 | 40 | TV Show |
| 19 | `0x269a34` | 8 | 42 | TV Season |
| 49 | `0x2699f4` | 9 | 7 | episodes / tracks |

Decisive: `mov r1,#8` at `0x16694c`, `#7` at `0x166998`, `#3` at `0x1662e4`, `#0` at `0x166328`,
`#1` at `0x166910`, `#9` at `0x166ae4`, `#2` at `0x166f54`, `#4` at `0x165b78`, `#5` at `0x1677ac`.
Mode 6 exists in the jump table but has no ordinal. All **H**.

##### (ii) Counts

Each lazily builds its list, then returns the count word.

| # | addr | callee | returns | conf |
|--:|---|---|---|:--:|
| 40 | `0x268df0` | `0x9de88` | **album count** (`+0xc6c`) | H |
| 41 | `0x268e48` | `0x9dec4` | **artist count** (`+0xc50`) | H |
| 42 | `0x268f78` | `0x9df94` | **genre count** (`+0xc44`) | H |
| 44 | `0x268e68` | `0x9df68` | **composer count** (`+0xc5c`) | M |
| 21 | `0x2699b4` | `0x9e13c` | **TV show count** (`+0xc7c`) | H |
| 22 | `0x26987c` | `0x9e120` | **season count** (`+0xc84`) | H |
| 45 | `0x269bd8` | `0x9e158` | **track count** (`+0xc90`, after `0xcc7a8` builds the list) | H |
| 43 | `0x26973c` | `0x9e008` | **playlist count − 1** — the impl does `sub r4, r0, #1`, excluding the master library playlist at index 0 | H |
| 25 | `0x26a1fc` | `0x9e16c` | count of the second playlist list (`+0xd08`) | M |

##### (iii) Category name at index → an `SMusicFilter` handle

Five byte-identical bodies differing only in the callee. Each callee bounds-checks against the
matching count, indexes the array, and tail-calls the string-pool fetch `0x0009f0c4`. The shim then
runs `0xc91e4`, which pulls `(name, kind)` out of the returned `SMusicFilter` value (`0x148da4`,
`0x148d9c`) and registers a fresh one with `0xc9220`.

| # | addr | callee | category | pool | conf |
|--:|---|---|---|---|:--:|
| 53 | `0x268fc4` | `0x9cabc` | **Artist** | `chunk+0xe0` | H |
| 54 | `0x268f88` | `0x9c9fc` | **Album** | `chunk+0x138` | H |
| 55 | `0x26909c` | `0x9ce04` | **Genre** | `chunk+0x190` | H |
| 56 | `0x269060` | `0x9ccc4` | **Composer** | `chunk+0xe0` | M |
| 57 | `0x269124` | `0x9d254` | **TV Show** | `chunk+0x47c` | H |
| 23 | `0x26910c` | `0x9d220` | **TV Season** — returns an **int**, not a string (`strhi r0,[r2]`) | — | H |

##### (iv) Apply a filter

Five ~250-byte bodies with one shape. If the filter's cached index (`filter+0x0c`) is `> 0`, fetch
the name at that index and `strcmp` it; on a hit reuse the cached index
(`165fb8: ldrne r1,[r5,#12]`). If the filter's string is empty, call the setter with **`-1`**
(`165ff0: mvn r1,#0`) — that is how a filter is *cleared*. Otherwise linear-scan the category. Every
setter bounds-checks and returns **`-50`** (`mvn r0,#49`) when the index is out of range, then runs
the invalidators for everything below it in the hierarchy.

| # | addr | category | setter | writes | conf |
|--:|---|---|---|---|:--:|
| 32 | `0x2698b4` | Artist | `0xb2620` | `db+0x10` | H |
| 33 | `0x26988c` | Album | `0xb25a8` | `db+0x18` | H |
| 34 | `0x269904` | Genre | `0xb2810` | `db+0x0c` | H |
| 35 | `0x2698dc` | Composer | `0xb26cc` | `db+0x14` | M |
| 37 | `0x26998c` | TV Show | `0xb2b44` | `db+0x20` | H |
| 38 | `0x269974` | TV Season — takes a plain `int`, no filter object | `0xb2adc` | `db+0x24` | H |

##### The rest, individually

| # | addr | behaviour | conf |
|--:|---|---|:--:|
| 24 | `0x269000` | `(lib, albumIndex)` → **album artwork**: bounds-check against the album count, read `db+0xc68[idx]`, `0x99fc4(db+0xd2c, that)`, wrap with `0x21be3c`, `Tracker<SImage>::Add` | H |
| 27 | `0x26959c` | calls all eight invalidators in a row (`0xa3620, 0xa34b8, 0xa3504, 0xa3450, 0xa3730, 0xa36fc, 0xa376c, 0xa37a8`) — **flush every cached list** | H |
| 29 | `0x268e58` | `0x914ac(db, 0, …)` — select playlist index 0, the **master library playlist** | H |
| 30 | `0x26994c` | scans the playlists comparing 64-bit IDs, then `0x914ac(db, i+1, …)`; falls back to the second list via `0x915a4` → **select a playlist**. The shim resolves its second argument through the `SPlaylist` accessor, so the caller passes a playlist handle | M |
| 31 | `0x26992c` | `(lib, includeMask, excludeMask)` — both remapped through `0xf5b74`, stored at `db+0xc2c` / `db+0xc30`, and tested against `track+0xb6`, the media-kind word from 11.4 → **set the media-kind include/exclude filter** | H |
| 36 | `0x268edc` | `0xb3a60(db, 1)` → `strb 1,[db+0xc94]`, enabling a stricter per-track eligibility test at `0x9388c`/`0x938ec` | M |
| 39 | `0x269380` | `0xb2698(db)` — invalidate albums, `strb 1,[db+0xc95]`, invalidate below. That byte makes `0x9de88` build the album list with mask `0x80000000` instead of `2`, and makes `0x11443c` keep only tracks with `track→0x20→0x15 & 2` → **switch album browsing into compilations mode** | M |
| 58 | `0x269160` | `(lib, index)` — bounds vs `db+0xc90`, `db+0xc88[idx]`, `0x9e418`, `0x21e408` → **track at index** | H |
| 59 | `0x269294` | `(lib, idLo, idHi)` — `0x9a848` on the library playlist → **track by 64-bit persistent ID** | H |
| 141 | `0x26a04c` | **no kind check** — `ldrb [((db+0xcfc)[0]→0)→0x0c, #0x4d9]` | M |
| 142 | `0x26a18c` | the same chain, byte `+0x4da` — two adjacent flag bytes in the library-playlist header | M |

The browse API is therefore: pick a mode (i), ask for a count (ii), pull entry *n* as a filter
(iii), hand that filter back to narrow the view (iv), then read tracks out with `#45`/`#58`. The
filter is a value object — `{ name, cachedIndex }` — so the same handle can be reused across
rebuilds, and an empty name clears the level.

#### `SPlaylist` — the class, its vtable, and the three ways to get one

`SPlaylist` (ctor `0x00235d30`, 20 bytes) is
`{ vptr, dbPlaylist +4, dbManager +8, generation +0xc, u8 lockMode +0x10 }` — the same shape as
`STrack`, and `IsValid` re-checks `generation == [[mgr+0xcf8]+0x10]` for the same reason. Vtable
`0x006770f0` (typeinfo `0x00665ee0` at `0x006770ec`):

| slot | fn | what it does |
|---|---|---|
| `+0x00` | `0x235d80` | destructor — `bx lr` |
| `+0x04` | `0x235d74` | deleting destructor → `0x25a8f4` |
| `+0x08` | `0x235cf0` | `IsValid()` |
| `+0x0c` | `0x235714` | `GetTrackCount()` → the u16 at `[[db+0x28]+0x32]` |
| `+0x10` | `0x2358fc` | `Play(...)` — index `-1` → `0x9388c` (shuffled start), else `0x92bbc` |
| `+0x14` | `0x235858` | `RemoveTrackAt(index, order, flag)` → `0x9c838`, `0x95958`, mark dirty `0xad504` |
| `+0x18` | `0x2359c0` | `RemoveAllTracks()` → `0x950b0`, mark dirty |
| `+0x1c` | `0x235da8` | `operator==` — `[a+4] == [b+4]` |

The table at `0x002a93d0` holds three Itanium-ABI **pointer-to-member-function** pairs
`{fn, adj<<1|virtual}`; all three have `adj = 0` and the virtual bit clear, which is why the shims
do `tst r5,#1 / add r1,r0,r5,asr #1`:

```
2a93d0:  00167600 00000000     ; used by ordinal 26   -> reads [db+0xd04]
2a93d8:  00166b90 00000000     ; used by ordinal 108  -> reads [db+0xcfc]
2a93e0:  00165dfc 00000000     ; used by ordinal 109  -> count [db+0xd00],
                               ;   element [[db+0xcfc] + idx*4 + 4]  <-- index+1
```

`0x0028a2a0` is their shared trampoline: call the member function (sret), pull the `dbPlaylist` out
of the returned temporary, wrap it in a fresh `SPlaylist` via `0x235d30`, `Tracker<SPlaylist>::Add`.
**Ordinal 109 skips playlist 0** — the master "all songs" playlist — so its index space is
"user playlists", not "all playlists". That off-by-one is exactly the sort of thing a port gets
wrong silently.

| # | addr | impl | args → return | conf |
|--:|---|---|---|:--:|
| 26 | `0x2691c8` | member `0x167600` over `[db+0xd04]` | `(lib, index)` → playlist handle | M |
| 108 | `0x2690d8` | member `0x166b90` over `[db+0xcfc]` | `(lib, index)` → playlist handle | M |
| 109 | `0x269200` | member `0x165dfc`, **index+1** | `(lib, index)` → playlist handle | H |
| 111 | `0x269864` | vt `+0x08` | `(playlist)` → bool `IsValid` | H |
| 112 | `0x2697bc` | `0x235c3c` — `ldm` of `[db+0x18]` | `(playlist)` → **64-bit persistent ID** in `r0:r1` | H |
| 113 | `0x26984c` | vt `+0x0c` | `(playlist)` → u16 **track count** | H |
| 114 | `0x2697cc` | `0x235c8c(&s, playlist)` (`0x9e42c` → UTF-16 + length → `0x223884`) then `0x11c708` | `(playlist, char* buf, int* len)` → **name** | H |
| 115 | `0x26974c` | `0x235790(&tmp, playlist, index, order)` → `Tracker<STrack>::Add` | `(playlist, index, order)` → track handle | H |
| 116 | `0x26981c` | vt `+0x14` | `(playlist, index, order, flag)` → bool **RemoveTrack** | H |
| 117 | `0x269804` | vt `+0x18` | `(playlist)` → bool **RemoveAllTracks** | H |

#### Ordinal 62 is the *now-playing playlist*, and 119–140 are its methods

This is the correction that matters most, because the object's name is not in the RTTI and the
obvious guess is wrong. `Metadata #62` is a lazy singleton getter:

```asm
00196418  ldr  r4, [pc, #32]      ; the global at 0x1081d9cc
00196420  ldr  r0, [r4]
00196428  bne  0x19643c
0019642c  mov  r0, #0x90          ; 144 bytes
00196430  bl   0x25a974           ; malloc
00196434  bl   0x196de0           ; construct
00196438  str  r0, [r4]
0019643c  ldr  r0, [r4]
```

Its constructor `0x00196de0` **calls the `SPlaylist` constructor `0x00235d30` first** and then
overwrites the vptr with `0x0066cad4`. So it is an anonymous `SPlaylist` subclass — anonymous in
the sense that the words at `0x0066cacc`/`0x0066cad0` are both zero, i.e. **its typeinfo pointer is
NULL**, which is precisely why no RTTI string names it. Its vtable mirrors `SPlaylist`'s slot for
slot and every override just takes the object's own lock and tail-calls the base.

What settles what it *is*: `0x000b36b8`, the shuffle setter on the underlying DB playlist, itself
calls `0x00196418` and `0x00235a54` to preserve the current track across a reshuffle. **The
singleton is the device's current / now-playing playlist.**

Layout (0x90 bytes), on top of the 20-byte `SPlaylist` base:

| offset | field |
|---|---|
| `+0x04` | `dbPlaylist*` — a *temporary* iTunesDB playlist, created on demand |
| `+0x08` | music DB manager, `[[0x1081da18]+0x1c]` |
| `+0x14` | `int currentIndex` (`-1` = none) |
| `+0x18` | mode byte |
| `+0x19` | **repeat-locked** flag — blocks ordinal 130 |
| `+0x1a` | **shuffle-locked** flag — blocks ordinal 123 |
| `+0x1c` | recursive lock (`0xc9a84` / `0xc9b58`) |
| `+0x24` | embedded 0x6c-byte query object (vtable `0x0066d6e0`): `{ vptr, SPlaylist source +4, SMusicFilter filters[5] at +0x18/+0x28/+0x38/+0x48/+0x58, int +0x68 }` |

and the DB playlist bits it drives: `+0xe4` u16 repeat mode (0/1/2, set by `0xb3658`), `+0xec`
bit 0 shuffle on (set by `0xb36b8`), `+0xed` bit 1 shuffle-by-album, `+0xe8` play-order generation
counter.

For the record, `SMusicFilter` is **16 bytes** — `{ vptr = 0x00668884, SString value +4, int kind
+0x0c }`, constructor `0x00148dd0` (which sets `kind = -1`), `0x00148d9c` = get kind,
`0x00148da4` = copy the string, `0x00148dfc` = destructor, single virtual `0x00148e48` =
`operator==`.

| # | addr | behaviour | args → return | conf |
|--:|---|---|---|:--:|
| 62 | `0x2691fc` | `b 0x196418` — **get the now-playing playlist singleton** | `()` → object pointer | H |
| 119 | `0x269660` | `0x19630c` — delete the temp DB playlist (`0x96358`), `+0x14 = -1`, clear `+0x18/19/1a`, reset the five filters from the guarded static at `0x1086c0c8` | `(p)` → void — **Clear** | H |
| 120 | `0x269650` | `0x1966c0(p, 0, 0, 0)` — shim forces all three; `0x196790` makes an empty `'file'`-type DB playlist, sets `+0xec` bit 6, then applies repeat and shuffle | `(p)` → void — **Create** | H |
| 121 | `0x269674` | **`mov r0,#0 ; bx lr`** | → 0 | H |
| 122 | `0x26967c` | **`mov r0,#0 ; bx lr`** | → 0 | H |
| 123 | `0x269714` | `0x1964bc` — no-op if `+0x1a`; saves the current track, `0xb36b8(db, on, 1)`, re-finds the index via `0x9e57c(track, 0x33, 0)` | `(p, bool on)` → void — **enable shuffle** | H |
| 124 | `0x26968c` | `0x196d48` → `0xb0d40` — reshuffle preserving the current track, then re-find the index | `(p)` → void — **new random order** | H |
| 125 | `0x269664` | **`ldr r0,[r0,#0x14] ; bx lr`** | `(p)` → current index, `-1` = none | H |
| 126 | `0x26966c` | **`ldrb r0,[r0,#0x18] ; bx lr`** | `(p)` → mode byte | H |
| 127 | `0x2695f4` | builds a temporary `STrack` on the stack (vtable word `0x00676608`, copies `+4/+8/+0xc` and byte `+0x10` from `Tracker<STrack>[a1]`), then `0x196638` → creates the playlist if absent and `0x88944(dbPlaylist, [track+4], 0)` | `(p, trackHandle)` → void — **append track** | H |
| 128 | `0x269730` | **`ldr r1,[r0] ; ldr r1,[r1,#8] ; bx r1`** → `0x1968b0`, `[this+4] != 0` | `(p)` → bool `IsValid` | H |
| 129 | `0x2696fc` | `0x196748` — sets `+0xec` bit 1 after an `IsValid` check | `(p, bool)` → void | M |
| 130 | `0x269710` | `0x196448` — no-op if `+0x19`; **maps 1↔2** then `0xb3658` | `(p, mode 0/1/2)` → void — **set repeat** | H |
| 131 | `0x269708` | **`strb r1,[r0,#0x1a] ; bx lr`** | `(p, bool)` → void — lock shuffle | H |
| 132 | `0x269700` | **`strb r1,[r0,#0x19] ; bx lr`** | `(p, bool)` → void — lock repeat | H |
| 133 | `0x269694` | `0x1965b4(&tmp, p, index, order)` (order 0 ⇒ `shuffle ? 2 : 1`) → `0x9c838` → `0x9e418` → `STrack` ctor → `Tracker<STrack>::Add` | `(p, index, order)` → track handle | H |
| 134 | `0x269720` | `0x12e520()` fetches the object registered under id `0x6600` (`0x1d3050` looks it up in the table at `0x108708b4`, `0x222dd0` calls its `vt+0x14`), then `0x12e434`: return `[that+0x118]` unless it is `-1`, else fall back to `#62()->vt+0x0c` = the singleton's **track count** | `()` → int | H |
| 135 | `0x26971c` | `0x196dac` — `[db+0xec] & 1` | `(p)` → bool shuffle on | H |
| 136 | `0x269688` | `0x196d08` — `[db+0xe4] == 2` | `(p)` → bool repeat-all | H |
| 137 | `0x269684` | `0x196ccc` — `[db+0xe4] == 1` | `(p)` → bool repeat-one | H |
| 138 | `0x269690` | `0x196688` — `[db+0xed]` bit 1 (the same bit that feeds arg 2 of `0x9c838`) | `(p)` → bool shuffle-by-album | H |
| 139 | `0x2695f0` | `0x196484` — `1 & ~([db+0xed] >> 5)`, i.e. **NOT** bit 5 | `(p)` → bool | M |
| 140 | `0x269718` | `0x196080` — `[db+0xe8]` | `(p)` → play-order generation counter | H |

Note the **1↔2 swap in ordinal 130**: it is the same public-vs-internal remap section 10a recorded
for `Audio #48`/`setRepeatMode`, reached through a different door. Public `0 = off, 1 = one,
2 = all`, and ordinals 136/137 read it back the same way round.

#### `STrack` (`0x0011e3a8`) — ordinals 63–107, 143–151

Vtable `0x00676608`. String getters take `(track, char* buf, int* len)`; scalar getters take
`(track)`; the date getters fill a **10-byte broken-down time** (`0x000e4d28` adds the constant
`0x83da4f80` = `-2 082 844 800`, the Mac-1904 → Unix-1970 offset, plus a 15-minute-unit timezone
offset from `0x000e5e94`, then formats via `0x000d8530`).

| # | addr | vt | record field | name / behaviour | conf |
|--:|---|---|---|---|:--:|
| 63 | `0x26a1cc` | `+0x00` | — | **`IsValid`** — record live *and* `db[0xcf8][0x10] == track+0x0c` | H |
| 64 | `0x269e10` | `+0x04` | `rec+0x108` (8 B) | **persistent ID / `dbid`** (`mhit+0x70`), returned in `r0:r1` | H |
| 65 | `0x269ff0` | `+0x08` | *(special)* | **file path / location** (`mhod` type 2; `0xb7858` + `0xa47a4`). Ordinals 97/98 parse its extension, which is the corroboration | H |
| 66 | `0x269f94` | `+0x0c` → `+0x48` | `rec+0x28`, pool `db+0xb4` | **Title** (`mhod` 1) | H |
| 67 | `0x269a54` | `+0x10` → `+0x4c` | `rec+0x34`, pool `db+0x138` | **Album** (`mhod` 3) | H |
| 68 | `0x269a98` | `+0x14` → `+0x50` | `rec+0x2c`, pool `db+0xe0` | **Artist** (`mhod` 4) | H |
| 69 | `0x269d40` | `+0x1c` → `+0x58` | `rec+0x40`, pool `db+0x190` | **Genre** (`mhod` 5) | H |
| 70 | `0x269b94` | `+0x20` → `+0x5c` | `rec+0x3c`, pool `db+0xe0` | **Composer** (`mhod` 12) | H |
| 71 | `0x269d84` | `+0x24` → `+0x60` | `rec+0x38`, pool `db+0x164` | **Grouping** (`mhod` 13) | H |
| 72 | `0x269ec0` | `+0x28` → `+0x64` | `rec+0x44`, pool `db+0x1bc` | **Kind / filetype** (`mhod` 6) | H |
| 73 | `0x269cb4` | `+0x2c` → `+0x68` | `rec+0x48`, pool `db+0x1e8` | **EQ preset** (`mhod` 7) | H |
| 74 | `0x269b50` | `+0x30` → `+0x6c` | `rec+0x4c`, pool `db+0x214` | **Comment** (`mhod` 8) | H |
| 75 | `0x269b0c` | `+0x34` → `+0x70` | `rec+0x50`, pool `db+0x240` | **Category** (`mhod` 9) | H |
| 76 | `0x269c10` | `+0x38` → `+0x74` | `[rec+0x10]+0x08`, pool `db+0x424` | **Description** (`mhod` 14) | H |
| 77 | `0x26a0f0` | `+0x3c` → `+0x78` | `[rec+0x10]+0x0c`, pool `db+0x47c` | **TV show name** (`mhod` 19) | H |
| 78 | `0x26a0d8` | `+0x40` | `[rec+0x10]+0x20` ← `mhit+0xd4` | scalar; the TV season/episode block | M |
| 79 | `0x26a0b8` | `+0xcc` | `[rec+0x10]+0x18` ← `mhit+0x8c` | **release date** — `memset(out,0,10)` then `0xe4d28`; returns 0/1 | M |
| 80 | `0x26a138` | — | — | **`bx lr`** — no-op, returns `r0` unchanged | H |
| 81 | `0x269af4` | `+0x80` | `rec+0x64` ← `mhit+0x6c` | bookmark time (ms) | M |
| 82 | `0x26a19c` | `+0x84` | `rec+0x5c` ← `mhit+0x44` | **start time** (ms) | H |
| 83 | `0x26a1b4` | `+0x88` | `rec+0x60` ← `mhit+0x48` | **stop time** (ms) | H |
| 84 | `0x269c6c` | `+0x8c` | `rec+0x58` / 1000 | **duration in seconds** (`bl 0x82694` with 1000) | H |
| 85 | `0x269c84` | `+0x90` | `rec+0x58` ← `mhit+0x28` | **duration in milliseconds** | H |
| 86 | `0x26a15c` | `+0x9c` | `rec+0x6c` ← `mhit+0x12c`, else `mhit+0x24` | **file size in bytes** | H |
| 87 | `0x269adc` | `+0xa0` | `rec+0x74` (h) ← `mhit+0x38` | **bitrate** | H |
| 88 | `0x26a1e4` | `+0xa4` | `rec+0x76` (h) ← `mhit+0x34` | **year** | H |
| 89 | `0x26a0a0` | `+0xa8` | `rec+0x7a` (sbyte) ← `mhit+0x1f` | **rating** (0–100) | H |
| 90 | `0x269fd8` | `+0xac` | `rec+0x80` (h) ← `mhit+0x2c` | **track number** | H |
| 91 | `0x269c54` | `+0xb0` | `rec+0x84` (h) ← `mhit+0x5c` | **disc number** | H |
| 92 | `0x26a034` | `+0xb4` | `rec+0x8c` ← `mhit+0x50` | **play count** | H |
| 93 | `0x269dc8` | `+0xd0` | `rec+0x1c & 0x40` ← `mhit+0xa4 == 1` | **has artwork** (returns `0x40`, not 1) | H |
| 94 | `0x269de0` | `+0xd4` | `rec+0x8b` bit 2 | remembers playback position (set from `mhit+0xa6` or mediatype bit 3) | M |
| 95 | `0x269df8` | `+0xd8` | `rec+0x8b` bit 4 ← `mhit+0xb0` | flag | L |
| 96 | `0x269e30` | `+0xdc` | mediatype `rec+0xb6` bit 3 | **is audiobook** | M |
| 97 | `0x269e60` | `+0xe0` | — | path ends `.m4a`/`.m4b`/`.m4p` (case-insensitive) → **is MPEG-4 audio** | H |
| 98 | `0x269e48` | `+0xe8` | — | path ends `.m4b` → **is audiobook file** | H |
| 99 | `0x269e78` | `+0xfc` | `rec+0x8b` bit 0 ← `mhit+0xa7` | flag ("flag4"/podcast) | M |
| 100 | `0x269ea8` | `+0x100` | mediatype `& 0x62` → 0/1 | **is video** (video ∪ music video ∪ TV show) | M |
| 101 | `0x269e90` | `+0x104` | mediatype bit 6 | **is TV show** | M |
| 102 | `0x26a134` | — | — | **`bx lr`** — no-op | H |
| 103 | `0x26a13c` | `+0x130` | `rec+0x7a` | **`SetRating(track, value)`** — clamps to `[0,100]`, writes the byte, then notifies via `0xb3ee0`/`0xb2bc4` | H |
| 104 | `0x269e28` | — | — | **`bx lr`** — no-op | H |
| 105 | `0x269f1c` | `+0xb8` | `rec+0x94` ← `mhit+0x58` | **date last played** → 10-byte time struct | H |
| 106 | `0x269be8` | `+0x94` | `rec+0x54` | a Mac-epoch timestamp → 10-byte time struct. **The `mhit` parser never writes `rec+0x54`**, so its source is elsewhere; most likely date added | M |
| 107 | `0x269f6c` | `+0x98` | `rec+0x68` ← `mhit+0x20` | **date modified** → 10-byte time struct | H |
| 143 | `0x26a05c` | `+0x44` → `+0x7c` | `[rec+0x10]+0x04`, pool `db+0x450` | **podcast URL** (`mhod` 16) | H |
| 144 | `0x26a174` | `+0xbc` | `rec+0x9c` ← `mhit+0x98` | scalar (skip count is the usual occupant of `mhit+0x98`) | M |
| 145 | `0x269f44` | `+0xc0` | `rec+0xa4` ← `mhit+0xa0` | a timestamp → 10-byte time struct | M |
| 146 | `0x269d28` | `+0x118` | `rec+0xe8` ← `mhit+0xcc` | scalar | L |
| 147 | `0x269cf8` | `+0x11c` | `rec+0xec` ← `mhit+0xb8` | scalar | L |
| 148 | `0x269d10` | `+0x120` | `rec+0xf0` ← `mhit+0xc8` | scalar | L |
| 149 | `0x269c9c` | `+0x124` | `rec+0xf8` (8 B) ← `mhit+0xbc/0xc0` | 64-bit value in `r0:r1` | L |
| 150 | `0x269e2c` | — | — | **`bx lr`** — no-op | H |
| 151 | `0x269f04` | `+0x128` | `rec+0x100` (8 B) ← `mhit+0xf8/0xfc` | 64-bit value in `r0:r1` | L |

Not exported, but worth knowing they exist: `STrack` vtable slots `+0x48`–`+0x7c` are the *inner*
string getters the public ones delegate to, and `+0x134`/`+0x138`/`+0x13c` are further methods with
no ordinal. `+0x18`, `+0x54`, `+0x108`–`+0x114` are NULL in the vtable.

### 11.6 Free stubs — exact, not approximate

Six ordinals are trivially reproducible byte-for-byte:

| # | addr | bytes | exact behaviour |
|--:|---|---|---|
| 80 | `0x26a138` | `e12fff1e` | `bx lr` — returns `r0` unchanged |
| 102 | `0x26a134` | `e12fff1e` | `bx lr` |
| 104 | `0x269e28` | `e12fff1e` | `bx lr` |
| 150 | `0x269e2c` | `e12fff1e` | `bx lr` |
| 121 | `0x269674` | `e3a00000 e12fff1e` | `return 0` |
| 122 | `0x26967c` | `e3a00000 e12fff1e` | `return 0` |

And four more are one instruction of real work on a caller-supplied pointer, so they can be
implemented exactly without knowing what the field means:

| # | addr | exact behaviour |
|--:|---|---|
| 125 | `0x269664` | `return *(u32*)(obj + 0x14)` |
| 126 | `0x26966c` | `return *(u8*)(obj + 0x18)` |
| 131 | `0x269708` | `*(u8*)(obj + 0x1a) = arg1` |
| 132 | `0x269700` | `*(u8*)(obj + 0x19) = arg1` |

Note `#80/#102/#104/#150` return **`r0` unmodified**, not zero. If the caller passed a handle in
`r0` it gets that handle back. Stubbing them as "return 0" is *not* the same thing.

### 11.7 What a caller must implement, and in what order

Two answers, because they are very different sizes.

**(a) For `Lost`, the only game that imports this framework — two ordinals.**

The eApp import block for `Metadata` in `Lost_1_1_2917525.bin` is at file `0x934`: name at `0x934`,
count `152` at `0x964`, thunk array at `0x96c` (VA `0x1800096c`), resolved-pointer array at
`0xbcc`, next-block magic `0x29061968` at `0xe2c`. Scanning every `B`/`BL` in the whole binary for
targets inside `0x1800096c..0x18000bcb` finds **exactly two ordinals used: `#62` and `#134`**. (The
same scan gives OpenGLES 16, AsyncFileIO 7, Audio 25, InputEvents 2, miscTBD 10, Settings 1 — all
plausible, so the method is sound; a 152-entry block is emitted whole regardless of use.)

Both live in one wrapper at `0x18006d48`:

```asm
18006d4c  ldr r4, [pc, #32]     ; 0x18041758, a lazily-created game object
18006d50  ldr r0, [r4]
18006d58  bne 0x18006d68
18006d60  bl  0x18006bd8        ; new(1) -- the result is never used before the call below
18006d68  bl  0x18000a64        ; Metadata #62  -> the database singleton
18006d70  b   0x18000b84        ; Metadata #134 -> an int
```

and its caller at `0x18006c40` samples `#134` **before and after** `Audio #40` (the music-track
registration from section 9a), returning `after-1` when the value changed and `-1` when it did not.

That reads exactly right against 11.5: `#62` is the now-playing playlist and `#134` is its **track
count**. The game is appending a track to the device's play queue through `Audio #40` and asking
the queue how long it is, so that `count-1` is the index of the track it just added.

To get `Lost` past this:

* `#62` must return a **non-NULL, stable pointer**. A zeroed `0x90`-byte block is enough — the game
  never dereferences it, it only hands it straight back to `#134`.
* `#134` must return a count that **increments by one across each `Audio #40`**. A constant makes
  `0x18006c40` answer `-1` forever, which is that function's failure value.

Nothing else in `Metadata` is reachable from this binary, so the rest of the framework can stay
unimplemented for `Lost`.

**(b) For a general `Metadata` implementation** the minimum viable set is:

1. `#0 MusicLibraryCreate()` → handle, `#1` to destroy it.
2. `#63 IsValid` → 1 (every `STrack` getter is gated on it), then the `STrack` getters that matter:
   `#66` title, `#68` artist, `#67` album, `#69` genre, `#85` duration-ms, `#90` track number,
   `#65` path.
3. The browse loop: a mode setter from group (i) — `#46` artist, `#47` album, `#48` genre,
   `#52` songs; the matching count from group (ii) — `#41`, `#40`, `#42`, `#45`; the name-at-index
   from group (iii) — `#53`, `#54`, `#55`; and the filter application from group (iv) — `#32`,
   `#33`, `#34`. Plus `#118` to read a filter's name and `#58` to turn a track index into a handle.
4. `#62` and `#134` for the now-playing queue, and `#125`/`#133` if the caller wants the current
   track.
5. The releases: `#60` (track), `#61` (filter), `#110` (playlist), `#5` (image), `#13` (image
   data), `#1` (library), `#3` (artwork library). A client that leaks will grow the `Tracker` until
   the growth allocation fails.

The expected call sequence is:

```
lib  = #0()                        ; SMusicLibrary handle
       #46(lib)                    ; browse mode := artists
n    = #41(lib)                    ; how many
flt  = #53(lib, i)                 ; i-th artist as an SMusicFilter handle
       #118(flt, buf, &len)        ; its display name
       #32(lib, flt)               ; narrow to that artist   (-50 = index out of range)
       #47(lib) ; m = #40(lib)     ; now browse that artist's albums
       #33(lib, albumFilter)       ; narrow again
t    = #45(lib)                    ; track count under the current filters
trk  = #58(lib, j)                 ; j-th track
       #63(trk)                    ; must be non-zero or every getter below returns empty
       #66/#67/#68/#85(trk, buf, &len)
       #60(trk) ; #61(flt) ; #1(lib)
```

Two behaviours a port must copy rather than invent, because a caller can see both:

* **`#43` returns the playlist count minus one** and `#109` indexes playlists **from 1**, because
  index 0 is the master library playlist. Off-by-one here silently hides or duplicates a playlist.
* **An `SMusicFilter` with an empty name clears its level** (`mvn r1,#0` at `0x165ff0`), and an
  out-of-range index returns **`-50`**, not `-1` and not 0.

### 11.8 What is not established

- **Category C = Composer is the weakest identification in 11.5.** It shares the artist string pool
  and its per-track extractor reads `rec+0x3c`, which the `mhod` dispatch fills from type 12
  (composer) — but the alternative reading, Album Artist (`mhod` 22), was reached independently
  from its descriptor bit. Composer wins because `mhod` 12 *is* `rec+0x3c` and because Composers is
  a real iPod 5G browse menu while Album Artist is not, but it is one inference deep.
- **Category G (`+0xc74`)** is a playlist-scoped list gated on `playlist+0xed & 0x80`. Only ordinal
  51 touches it, and there is no exported count, name getter or filter setter for it. What it lists
  is unknown.
- **`#134`'s upstream.** It is measured to read `[obj+0x118]` of whatever is registered under id
  `0x6600`, falling back to the singleton's `vt+0x0c`. The fallback is definitely the track count;
  whether `[obj+0x118]` is always the same number is inference from one call site.
- **`rec+0x54` (`#106`)** is a Mac-epoch timestamp that the `mhit` parser never writes. Something
  else fills it, so "date added" is a guess.
- **`mhit` offsets `0xb0`, `0xb8`, `0xbc`, `0xc8`, `0xcc`, `0xf8`** (ordinals 95, 146–149, 151) are
  reported as raw offsets on purpose; naming them would be guessing at the 5G-era `mhit`
  extensions.
- **The house heuristics both failed here and the failures are worth carrying forward.** Ordinals
  are *not* alphabetical (11.4), and the one class that would have been named by RTTI — ordinal
  62's singleton — deliberately has a NULL typeinfo pointer, so it had to be identified from its
  constructor calling `SPlaylist`'s and from who else calls `0x00196418`.
- Nothing here has been executed. All of it is static reading of `osos.bin`, cross-checked between
  the shim, the implementation, the vtable and the iTunesDB parser — but never against a running
  device or emulator. The `mhod` type numbers in 11.4 do agree with the publicly documented
  iTunesDB layout for all sixteen cases, which is the strongest external check available.

## 12. Lost's draw path

Target for this section: **Lost** (`Games_RO/1B200`, `Lost_1_1_2917525.bin`, 268 140 bytes, load
base `0x18000000`). All `osos` addresses are file offsets (`VA = 0x10000000 + N`); all game
addresses are given as file offsets in the eApp with their VA `0x18000000 + N` where it matters.

The four unknowns — **`#152`, `#153`, `#159`, `#164`** — turned out **not** to be a drawing path at
all. They are the render-server lifecycle: load a firmware image, boot it, select a pipeline, shut
it down. Lost draws with the perfectly ordinary `#137 glVertexAttribPointer` / `#37 glDrawArrays`
pair, from **nine** call sites. Everything below is static reading of `osos.bin` and the eApp;
nothing was executed.

### 12.1 The eApp import block, and why "Lost never calls glDrawArrays" is a symptom

Lost's OpenGLES import block is at file `0x2c`: name at `0x2c` (32-byte buffer), 16-byte interface
hash at `0x4c`, count `0xb3` = 179 at `0x5c`, next-block name pointer at `0x60`, then **179 thunks
at `0x64`** (`e59ff2c4` = `ldr pc,[pc,#708]`), then the resolved-pointer array at `0x330`. So

```
thunk(ordinal i) = VA 0x18000064 + 4*i
```

Scanning every `B`/`BL` in the binary for targets in `0x18000064..0x1800032f` gives **16 ordinals
actually reached, 67 call sites**:

| ordinal | name | sites |
|--:|---|--:|
| 0 | `glActiveTexture` | 1 |
| 4 | `glBindTexture` | 1 |
| 12 / 13 | `glClear` / `glClearColor` | 2 / 2 |
| 19 | `glCompressedTexImage2D` | 1 |
| **37** | **`glDrawArrays`** | **1** |
| 40 | `glEnableVertexAttribArray` | 16 |
| 99 | `glTexImage2D` | 1 |
| **137** | **`glVertexAttribPointer`** | **16** |
| 147 | 4-component uniform setter (see 12.7) | 6 |
| 149 | `glUniformMatrix4xvAPPLE` | 11 |
| 152 / 153 / 157 / 159 / 164 | this section | 2 / 3 / 1 / 1 / 2 |

Most of those single-site ordinals are **one-instruction veneers** in a hand-written GL wrapper
layer at `0x71d0..0x7350`, e.g.

```asm
7260:  eaffe38c   b   0x98        ; glClearColor
7340:  eaffe36c   b   0xf8        ; glDrawArrays
```

so "one call site for `#37`" means one *veneer*; the veneer at `0x7340` has **nine** callers
(`0x520c`, `0x7e78`, `0x8bb4`, `0x161bc`, `0x20f44`, `0x3b5c4`, `0x3b728`, `0x3b898`, `0x3ba88`).
Each of those is a straight-line block of the form

```
#159(pipeline)  →  #149 glUniformMatrix4xvAPPLE  →  N× (#137 glVertexAttribPointer, #40 glEnableVertexAttribArray)  →  #37 glDrawArrays
```

The canonical one is `0x50f4`, the sprite/geometry flush:

```asm
50f4:  push {r2,r3,r4,r5,r6,r7,r8,lr}
50fc:  ldr  r0,[r0,#4]        ; vertex count
5100:  cmp  r0,#0
5104:  ble  0x5210            ; nothing batched -> return without drawing
...
5140:  bl   0x288             ; #137 glVertexAttribPointer(0, 4, 0x140c, 0, 0, [r4+8])
5180:  bl   0x288             ; #137                     (1, 2, 0x140c, 0, 0, [r4+16])
51c0:  bl   0x288             ; #137                     (2, 4, 0x140c, 0, 0, [r4+12])
5204:  ldm  r4,{r0,r2}        ; r0 = primitive mode, r2 = vertex count
520c:  bl   0x7340            ; #37 glDrawArrays(mode, 0, count)
```

**So an emulator that sees `#4/#12/#13/#99/#159` but never `#137/#37` is watching a game that never
got to its renderer, not a game using a hidden blit path.** There is no immediate-mode entry, no
`glDrawTex*OES`, and no sprite/blit ordinal in this interface. The single non-`glDrawArrays` way to
put pixels on screen is the system overlay quad in 12.5, which Lost never arms.

Two measured details that will silently destroy geometry in a re-implementation:

* **`0x140c` is `GL_FIXED`, not `GL_FLOAT`.** Lost's vertex type constant (literal at `0x5214`) is
  `0x0000140c`. Apple's own present path uses the same constant (literal at `0x26b7fc`). Vertex
  attributes are **16.16 fixed point**. Uniforms are IEEE floats (`0x3f800000` = 1.0f throughout).
* **`0x84f5` is this driver's `GL_TEXTURE_2D`.** Lost funnels every target through a mapper at
  `0x8634`:

  ```asm
  8634:  sub   ip, r0, #0xd00
  8638:  subs  ip, ip, #0xe1        ; ip = target - 0x0DE1
  863c:  ldreq r0, [pc]             ; -> 0x000084f5
  8640:  bx    lr
  ```

  i.e. `GL_TEXTURE_2D (0x0DE1) → 0x84F5`. Apple accepts both, and treats them as the *same*
  binding: `glBindTexture` (`0x26c614`) admits `0x0DE1`, `0x84F5` and `0x8513` (cube map) and then
  stores the name in **one slot per texture unit**, `ctx[0x94 + 4*activeUnit]`, with no per-target
  separation. An emulator that keys texture bindings on `0x0DE1` alone drops every bind Lost makes.

### 12.2 `#152` — start the render server / reset the GL context

`0x0026b138`, three arguments, returns `int`.

```c
int glStartRenderServerAPPLE(int unused_r0, int *outA, int *outB);
```

`r0` is read into no register and never used; Lost passes 0. Body, measured:

```asm
26b148:  mov  r6,#5 ; mov r5,#1
26b154:  bl   0x289650          ; -> singleton, then 0x1b0284  (boot the render server)
26b158:  bl   0x2895d0          ; -> singleton, then 0x1b03ec(ctx,1)  (mark running)
26b164:  bl   0x27f044          ; (5, 1) -- allocate the command ring
26b174:  bl   0x27ed88          ; pick the first free ring buffer  (always returns 0)
26b17c:  cmpeq r4,#0
26b184:  bne  <return 0>
26b1cc:  bl   0x27f3b8          ; emit a packet, opcode 5, on stream 0x1080009c
26b1d8:  bl   0x7ccd0           ; bzero(0x1084ba98, 0xac)   -- overlay descriptor
26b1e4:  bl   0x7ccd0           ; bzero(0x1084b898, 0x200)  -- #159 pipeline cache
26b1ec:  str  #1,[r7]           ; *outA = 1
26b1f4:  str  #2,[r8]           ; *outB = 2
26b1f8:  bl   0xce0fc           ; reset the GL context
26b1fc:  mov  r0,#1             ; return 1
```

* `0x27f044(5,1)` is the ring allocator: `bzero(0x10871448, 0x144)`, then **16 buffers of `0x2000`
  bytes** (`0x7a81c(0x2000, 1)`) at `[0x10871448 + 16*i + 0x44]`, spinning until each allocation
  succeeds. Return value is the accumulated error from `0x27ee5c`; `#152` returns **0** if that or
  `0x27ed88` is non-zero, **1** otherwise.
* `0x7ccd0` is `bzero(ptr, len)` (`mov r2,#0` then 32-byte `stmia` blocks).
* `0xce0fc` is a **full GL state reset**: `bzero(0x1084bbc4, 0x284)` then `[+0x264]=4`,
  `[+0x268]=4`, `[+0x26c]=1`, `[+0x270]=1`, `byte[+0x274]=1`, `byte[+0x280]=1`. `0x1084bbc4` is the
  GL context — the same base whose `+0x88` is the error word used by `#53 glGetError`.

`*outA = 1` and `*outB = 2` are **hard constants**, not derived from anything. Lost discards both
(`push {r2,r3,r4,lr}; mov r2,sp; add r1,sp,#4; ... pop {r2,r3,r4,pc}` at `0x777c`), so what they
mean is *not established*. Buffer count / backbuffer id and major/minor version both fit; nothing
in the image decides it.

### 12.3 `#153` — stop the render server, destroy every GL object

`0x0026b87c`, no arguments used, returns 1. Lost passes `r0 = 0`.

```asm
26b880:  bl 0x26e4b0   ; ordinal #41 -- emits opcode 9 and waits => glFinish
26b884:  bl 0x27f0cc   ; free all 16 ring buffers, drain 4 queues
26b888:  bl 0xdf224    ; walk a linked list at +0x1c freeing every node and its [+4]
26b88c:  bl 0x289640   ; -> 0x1b03b4: byte[0x108cbec8] = 0, byte[ctx+0x2a] = 0
26b890:  bl 0x2895e4   ; -> 0x1b02d8(ctx,1) + task teardown
26b894:  mov r0,#1
```

`0xdf224` frees a linked list of driver objects — this is the **texture/object destructor**. Taken
with `#152`'s `0xce0fc`, the pair is a *hard reset*: after `#153` … `#152` there are no texture
names, no attribute arrays and no pipeline cache left. Everything the game had must be recreated.

This also settles a neighbour: **`#41` is `glFinish`** (`0x26e4b0` builds a 16-byte header with the
low 5 bits set to `9` and blocks), which is exactly where the alphabetical ordering puts it —
between `#40 glEnableVertexAttribArray` and the `glFlush`/`glFramebuffer*` group.

### 12.4 `#164` — hand the driver the render-server image, and `rserver.bin`

`0x0026b580`, three arguments, returns 0/1.

```c
int glSetRenderServerImageAPPLE(int use, const void *image, int size);
```

```asm
26b580:  movs r3,r0          ; r3 = use
26b584:  mov  r0,r1          ; r0 = image
26b588:  mov  r1,r2          ; r1 = size
26b594:  moveq r1,#0 ; moveq r0,#0     ; use == 0  ->  clear
26b59c:  bl   0x2895b0       ; -> singleton, then 0x1b021c(ctx, image, size)
26b5a4:  movne r4,#1
```

`0x1b021c` is measured to: free whatever is in the driver's dynamic buffer at `ctx+24`
(`0x1afca8`), and if `image != 0 && size > 0`, allocate `size` bytes (`0x1afcd0` → `0x25a944`) and
`memcpy` the image in (`0x7cae0`). It returns 1 in both the store and the clear case, so `#164`
returns **1** unless the allocation failed.

Lost uses it from one helper at `0x8600`:

```asm
8600:  push {r4,r5,r6,lr}
8604:  mov  r5,r0 ; mov r4,r1
8608:  mov  r0,#0
8610:  bl   0x2c8       ; #153  -- stop
861c:  mov  r0,#1 ; mov r1,r5 ; mov r2,r4
8620:  bl   0x2f4       ; #164(1, image, size)
862c:  b    0x777c      ; #152(0,&a,&b)  -- restart
```

and the shutdown path at `0x3d43c` does the mirror image: `#153(0)`, `#164(0,0,0)`, `#152(0,&a,&b)`.

**The image is a file on disc.** At `0x3d5d0` Lost allocates 512 000 bytes (`0x6bd8(0x7d000)`),
async-reads a file whose name literal sits at `0x3d888` — `"rserver.bin"` — via `0x4910`, sets its
state byte `[r9+8] = 1`, and then in later frames waits for `[r9+8] == 2` before calling
`0x483c` for the transferred length and `0x8600(buffer, length)`. Until then it takes the branch at
`0x3d64c` straight to `0x3d82c`, which does nothing but `#157`.

`Games_RO/1B200/rserver.bin` exists: **105 020 bytes**, a 512-byte zero header then dense binary
(63 931 non-zero bytes). The alternative branch at `0x3d5b0` uses an *embedded* image at
`0x1803eec8` with its length at `0x1803eecc`; in this build both words are zero, so **Lost always
takes the file path**.

**Practical consequence.** If `AsyncFileIO` never completes the `rserver.bin` read, Lost's state
byte stays at 1 and the game loops forever in a present-only path. That is a state consistent with
"nothing renders", and it is worth ruling out first in the emulator.

### 12.5 `#159` — select one of fifty built-in pipelines

`0x0026a9e8`, **one** argument, returns 1.

```c
int glSetPipelineAPPLE(unsigned index);   // index 0..49
```

```asm
26a9ec:  ldr  r1,[pc,#1620]   ; 0x1084bb84
26a9f4:  cmp  r7,#0x31
26aa0c:  sub  r2,r1,#0x40     ; 0x1084bb44
26aa10:  addls pc,pc,r7,lsl #2
26aa14:  b    0x26ae9c        ; default (index > 49): falls into the common tail with r4=r5=0
```

`pc` reads as `0x26aa18`, so the table is `0x26aa18..0x26aadc` — exactly **50 entries, 0..49**.
Every case does the same shape of work: pick **two** driver objects and their two code pointers.

* **Indices 0–40 pick a fixed pair of statics.** e.g. index 2:

  ```asm
  26ab08:  ldr r4,[pc,#1348]   ; 0x108006bc
  26ab0c:  ldr r5,[pc,#1352]   ; 0x1081ba00
  26ab10:  add r0,r4,#28       ; code A = obj A + 28
  26ab14:  add r6,r5,#28       ; code B = obj B + 28
  ```

  The 33 distinct literals are all in `0x108000a0..0x1081cf0c`, which is **past the end of the
  `osos` image** (`0x10735a00`) — uninitialised/driver-owned memory. Their contents are therefore
  *not recoverable from the image*, and what each of the 50 indices means cannot be read out
  statically. Index **49 is an alias of index 13** (both jump to `0x26ab94`, `0x1080039c` +
  `0x108065f4`).
* **Indices 41–48 pick a pair out of two 16-word tables** at `0x1084bb44` (objects) and
  `0x1084bb84` (code pointers), e.g. index 41 reads `[+0]/[+4]` and index 48 reads `[+0x38]/[+0x3c]`.
  These eight are the **application-loadable slots**; see `#160` below.

The common tail at `0x26ae9c`:

```asm
26ae9c:  ldr r8,[pc,#596]      ; 0x1084b898  -- the per-index cache
26aea4:  ldr r1,[r8,r7,lsl #3] ; cached object A for this index
26aeac:  cmp r1,r4
26aeb0:  beq <skip>            ; already selected: emit nothing
...
26aef8:  orr r1,r1,r2          ; r2 = 0xe0 + (objA[24] << 3)
26af28:  bl  0x27f180          ; emit: (stream, hdr0..2, tag=index, ptr=objA, len=28, ptr2=objA+28)
26af2c:  str r4,[r8,r7,lsl #3]
...                             ; same again for object B into [r8 + index*8 + 4]
26afbc:  ldr r0,[r4,#4]
26afc4:  tst r0,#0x10000
26afd0:  bl  0x26e240          ; #39 glEnable      (bit set)
26afd8:  bl  0x26d6e8          ; #35 glDisable     (bit clear)
26b030:  bl  0x27f3b8          ; emit the select packet
26b038:  str r7,[0x10800098]   ; remember the current index
26b040:  mov r0,#1
```

So `#159` (a) uploads the two 28-byte program descriptors and their code, but **only when the index
changed** — the cache at `0x1084b898[index*8]` / `[index*8+4]` is what `#152` bzeroes for `0x200`
bytes (64 slots) — (b) toggles one GL cap from bit `0x10000` of `objA[4]`, and (c) records the
index at `0x10800098`.

Lost wraps it at `0x723c` as a *set-and-return-previous*:

```asm
723c:  ldr r1,[pc,#20]     ; 0x18060910
7244:  ldr r4,[r1,#0xec4]  ; previous
7248:  str r0,[r1,#0xec4]
724c:  bl  0x2e0           ; #159
7250:  mov r0,r4           ; return previous
```

Indices Lost is measured to use: **1, 2, 3, 14, 18, 39** as literals (`0x3b484`, `0x3b65c`,
`0x3b7a0`, `0x6388`, `0x7dd0`, `0x7de0`/`0x8b18`), plus values read from its own scene data. All are
< 41, i.e. **Lost only ever uses built-in pipelines and never loads one of its own.**

### 12.6 The neighbours that pin the reading down

Reading `152..166` as one private block makes all of them consistent:

| # | addr | what it is |
|--:|---|---|
| 152 | `0x26b138` | start render server + reset GL (12.2) |
| 153 | `0x26b87c` | stop render server, destroy objects (12.3) |
| 154 | `0x26b804` | clamps `r1` into `[0,6]`, emits a packet — a 7-value mode setter |
| 155 | `0x26b108` | `ldr r0,[0x1080009c]` then `mov r0,#0` — always returns **0** |
| 156 | `0x26b11c` | `mov r0,#1; bx lr` — always returns **1** |
| 157 | `0x26b674` | present (12.7) |
| 158 | `0x26b3f0` | 6-case dispatch on `r0 ∈ [0x3f000, 0x3f005]` — a private get/set enum block |
| 159 | `0x26a9e8` | select pipeline (12.5) |
| 160 | `0x26b214` | **load** a pipeline into slot 0..7 |
| 161 | `0x26b334` | set the overlay image |
| 162 | `0x26b5b0` | arm the overlay quad |
| 163 | `0x26b124` | `str #0,[0x1084ba98]` — disarm the overlay |
| 164 | `0x26b580` | set the render-server image (12.4) |

**`#160(unsigned slot, int size, const void *blob)`** is the loader that closes the loop on `#159`:

```asm
26b220:  cmp r4,#8  ; popcs                 ; slot must be < 8
26b22c:  cmp r1,#0x38 ; popcc               ; size must be >= 56
26b234:  cmp r2,#0 ; popeq                  ; blob must be non-NULL
26b24c:  mov r0,#28 ; bl 0x7a794            ; new obj A (28 bytes), into 0x1084bb44[slot*8]
26b268:  mov r0,#28 ; bl 0x7a794            ; new obj B,            into 0x1084bb44[slot*8+4]
26b294:  ... memcpy 28 bytes of blob into obj A, then obj A[24] bytes of code -> 0x1084bb84[slot*8]
26b2d8:  ... same for obj B from blob+28+lenA        ->                          0x1084bb84[slot*8+4]
26b308:  ldr r1,=0x1084b898 ; str #0,[r1 + slot*8 + 0x148] ; and +0x14c
26b31c:  mov r0,#0x3000 + 0xc                        ; returns 0x300C
```

`0x148 = 41*8`. **`#160` invalidates exactly the `#159` cache entries for indices `41+slot`** — which
is the independent proof that `#159` indices 41–48 are user slots 0–7, and that the fixed cases
0–40 are the driver's own built-ins.

**`#161(int w, int h, const void *pixels)`** (`w,h ≤ 64`) is the most useful function in the whole
block, because it calls `glTexImage2D` itself and therefore *documents its own ABI*:

```asm
26b36c:  ldr r0,=0x84c0 ; bl 0x26c534   ; #0  glActiveTexture(GL_TEXTURE0)
26b378:  mov r1,#0x40   ; bl 0x26c614   ; #4  glBindTexture(0x84F5, 64)
26b384:  ldr r2,=0x8033 ; ldr r1,=0x1908 ; mov r0,#0
26b390:  stmib sp,{r0,r1,r2,r9}         ; border=0, format=GL_RGBA, type=UNSIGNED_SHORT_4_4_4_4, pixels
26b394:  mov r2,r1                      ; internalformat = GL_RGBA
26b398:  mov r1,#0                      ; level
26b39c:  mov r0,r8                      ; target = 0x84F5
26b3a0:  mov r3,r4                      ; width
26b3a4:  str r5,[sp]                    ; height
26b3a8:  bl  0x270240                   ; #99 glTexImage2D
26b3b4:  stmib r0,{r4,r5}               ; 0x1084ba98[+4]=w, [+8]=h
```

The overlay lives in texture name **64** and is **RGBA4444**, at most 64×64.

**`#162(int w, int h)`** arms it: sets `[0x1084ba98] = 1`, writes a 4-vertex quad of size `w×h` at
`+0x4c` (positions) and `+0x6c`/`+0x8c` (texcoords) in **16.16 fixed point** (`lsl #16`
throughout), and builds the 4×4 matrix at `+0x0c` via

```asm
26b640:  mov r3,#0x3f800000        ; 1.0f
26b64c:  add r1,r3,#0x3f00000      ; 0x43700000 = 240.0f
26b658:  add r2,r1,#0x300000       ; 0x43a00000 = 320.0f
26b664:  add r0,ip,#12 ; bl 0x27356c   ; ortho(dst, 0, 320.0f, 0, 240.0f, -1.0f, 1.0f)
```

— which incidentally confirms the framebuffer is **320×240** and that uniforms are IEEE floats even
though attributes are `GL_FIXED`.

### 12.7 `#157`, and the one blit path that does exist

`0x0026b674`. Two arguments (Lost passes `0,0`); neither is read. It is gated on the overlay:

```asm
26b680:  ldr r0,[0x1084ba98]
26b68c:  beq 0x26b788             ; overlay disarmed -> just flush and return
```

If armed, it draws the overlay quad **through the ordinary vertex-array path**, bracketed by a full
save/restore of the state it disturbs:

```asm
26b6a4:  ldrh sl,[r4,#0x5c]       ; r4 = ctx + 0x200  -> save halfword at ctx+0x25c
26b6a8:  ldr  r7,[r5,#0x94]       ; save texture bound on the active unit
26b6bc:  bl   0xf0                ; save 56 bytes of attribute-array state from ctx+0x9c
26b6c4:  bl   0x26c534            ; #0   glActiveTexture(0x84C0)
26b6d0:  bl   0x26c614            ; #4   glBindTexture(0x84F5, 64)
26b6d8:  bl   0x26a9e8            ; #159(49)
26b6ec:  bl   0x271e0c            ; #125 glUniformMatrix4fv(0, 1, 0, ovl+0x0c)
26b6f4:  bl   0x26e43c            ; #40  glEnableVertexAttribArray(0)
26b6fc:  bl   0x26e43c            ; #40  glEnableVertexAttribArray(1)
26b71c:  bl   0x273050            ; #137 glVertexAttribPointer(0, 4, 0x140c, 0, 0, ovl+0x4c)
26b73c:  bl   0x273050            ; #137 glVertexAttribPointer(1, 2, 0x140c, 0, 0, ovl+0x8c)
26b74c:  bl   0x26d954            ; #37  glDrawArrays(7, 0, 4)
26b754:  strh sl,[r4,#0x5c]       ; restore ctx+0x25c
26b758:  strh #3,[r4,#0x5e]       ; ctx+0x25e = 3
26b760:  bl   0x26a9e8            ; #159(previous, read from [0x10800098] on entry)
26b76c:  bl   0x26c614            ; restore glBindTexture
26b774:  bl   0x26c534            ; restore glActiveTexture
26b784:  bl   0xf0                ; restore the 56 bytes at ctx+0x9c
26b7cc:  bl   0x27f3b8            ; emit a packet with opcode 6  -- the swap
26b7d8:  bl   0x27f128            ; kick the ring
26b7dc:  mov  r0,#1
```

This resolves the "walks the attribute arrays at `+0x5c`" note in `opengles-names.json`: the
save/restore pair is `ldrh`/`strh` on **`ctx + 0x200 + 0x5c` = `0x1084be20`**, plus a hard
`strh #3` into the adjacent halfword at `+0x5e` (attributes 0 and 1). The 56 bytes copied to and
from `ctx+0x9c` are the `glVertexAttribPointer` descriptors themselves. Whether `+0x25c` is the
enable mask and `+0x25e` a dirty mask, or the reverse, is **not established**; only that `#157`
restores the first and force-writes 3 into the second.

`glDrawArrays(7, ...)` matches §5 ("mode 7 is a quad list, not a fan") — a 4-vertex quad, and the
count is 4, not 6. That is the *only* blit-like path in the interface, it is Apple's system overlay
(a ≤64×64 RGBA4444 image), and **Lost never calls `#161`, `#162` or `#163`, so for Lost the whole
branch is skipped and `#157` is nothing but "emit opcode 6 and kick the ring".**

`#147` (`0x271678`), which Lost calls once per draw batch, takes 5 arguments, returns immediately
when the first is `-1`, and emits a 4-value packet — a `location, x, y, z, w` uniform setter. Given
`#148 glUniform4xvAPPLE` and `#149 glUniformMatrix4xvAPPLE` on either side, it reads as the scalar
form, `glUniform4xAPPLE`. That naming is inference, not measurement.

### 12.8 `#99 glTexImage2D`, and `GL_LUMINANCE_ALPHA`

`0x00270240`. It pushes 13 registers (52 bytes) then `sub sp,#68`, so the incoming stack arguments
start at `sp+120`. That gives the signature — and `#161`'s own call site (12.6) confirms every slot
independently:

```c
void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const void *pixels);
```

Validation, in order, each failure storing an error at `ctx+0x88` and returning:

| check | error |
|---|---|
| `width < 0 \|\| height < 0 \|\| border < 0` | `0x0501` `GL_INVALID_VALUE` |
| `internalformat != format` | `0x0502` `GL_INVALID_OPERATION` |
| `format - 0x1906 > 4` (i.e. not `ALPHA`/`RGB`/`RGBA`/`LUMINANCE`/`LUMINANCE_ALPHA`) | `0x0500` |
| `target >= 0x8517`, or not `0x0DE1` and not `0x84F5` | `0x0500` |

**`0x190A GL_LUMINANCE_ALPHA` is fully accepted.** There is nothing to reject.

Then the whole thing is a packet builder — **no pixel conversion happens on the CPU side at all**:

```asm
2702dc:  ldr r2,[sp,#132]      ; type
2702e0:  mov r1,r7             ; format
2702e4:  mov r0,r9             ; width
2702e8:  bl  0xe3158           ; bytesPerRow(width, format, type)
2702ec:  mla r5, r6, r0, r5    ; r5 = height*rowBytes + 36
...
270370:  stm sp,{r0,r1,r2,fp}  ; tex object, &params, 36, pixels
27037c:  bl  0x27f180
```

`0x27f180` splits the payload across the 8 KB ring buffers with a 16-byte chunk header, copying the
36-byte parameter block first and then **`height*rowBytes` bytes verbatim from the caller's
pointer**. The parameter block is `{target, level, 0, width, height, 1, format, type}`.

`0xe3158(width, format, type)` is the whole of the layout rule:

| format | type | bytes/texel |
|---|---|--:|
| `0x1906 GL_ALPHA` | *ignored* | 1 |
| `0x1907 GL_RGB` | `0x1401 UNSIGNED_BYTE` | 3 |
| `0x1907 GL_RGB` | `0x8363 UNSIGNED_SHORT_5_6_5` | 2 |
| `0x1908 GL_RGBA` | `0x1401 UNSIGNED_BYTE` | 4 |
| `0x1908 GL_RGBA` | `0x8033`/`0x8034` (4444 / 5551) | 2 |
| `0x1909 GL_LUMINANCE` | *ignored* | 1 |
| **`0x190A GL_LUMINANCE_ALPHA`** | ***ignored*** | **2** |

```asm
e31c0:  mov r1,#2         ; the LUMINANCE_ALPHA arm
e31c8:  mov r1,#1         ; ALPHA / LUMINANCE
e31cc:  mul r0,r1,r0      ; rowBytes = bpp * width
e31d0:  bx  lr
```

So for Lost's uploads:

* **`rowBytes = 2 * width`, exactly. No alignment, no padding, ever.** `glPixelStorei` (`#84`) is
  never consulted by this path — `glTexImage2D` does not read any unpack state, so
  `GL_UNPACK_ALIGNMENT` is irrelevant here. A 122×10 `LUMINANCE_ALPHA` texture is `244*10 = 2440`
  bytes; 42×10 is `84*10 = 840` bytes; total payload is `36 + 2*width*height`.
* **Two bytes per texel, and the driver stub does not reorder them.** The bytes go into the ring
  unmodified, so the pair is whatever `GL_LUMINANCE_ALPHA` means in the GL spec — **luminance byte
  first, alpha byte second**, row-major, top row first. This is *inference from the absence of any
  swizzle*, not a positive measurement: the consumer is the render-server firmware in
  `rserver.bin`, which is not disassembled here. If text comes out inverted, the L/A swap is the
  first thing to try, and it is a one-byte experiment.
* `type` is not even looked at for `ALPHA`, `LUMINANCE` and `LUMINANCE_ALPHA`; `0x1401` is simply
  the only sensible value.
* 122×10 and 42×10 are **not powers of two**, and this driver takes them without complaint —
  consistent with `0x84F5` being the real 2D target.

**So the emulator's drop rule is wrong on both counts:** the format/type pair is legal, and the
data is a tightly packed `L,A` byte pair array with `rowBytes = 2*width`.

### 12.9 Cross-check against `opengles-names.json`

The alphabetical heuristic runs out at **`#138 glViewport`**, which is the last name in `gl*` order.
`#139..#151` are the `*APPLE*` extension block, and **`#152..#178` are not GL entry points at all** —
they are the render-server/context layer, which is exactly why `#157` was already known to be
"present/flush" and why no `gl` name fits it. So:

* There is **no conflict**: alphabetical order does not constrain `#152/#153/#159/#164`, because
  those ordinals sit past the end of the alphabetical run. Anyone trying to fit `glTex*`-ish names
  into `#152`/`#153` because they sit "after `glVertexAttrib4xvAPPLE`" would be wrong.
* The heuristic *did* pay off on one neighbour: `#41` is `glFinish`, exactly where the ordering
  predicts, confirmed independently from `#153`'s use of it and from its opcode-9 wait.
* `#127`/`#128`, still null in the JSON, are almost certainly `glUseProgram` and
  `glValidateProgram` — they sit between `#126 glUnmapBuffer` and `#129 glVertexAttrib1f`. This
  matters because it rules out reading `#159` as `glUseProgram`: the interface already has one.

Suggested entries:

```
"41":  "glFinish",
"152": "start render server / reset GL context (unusedR0, int* outA=1, int* outB=2) -> 0/1",
"153": "stop render server, glFinish + free ring + destroy all objects -> 1",
"159": "select built-in pipeline by index 0..49 (41..48 = slots loaded by #160) -> 1",
"160": "load pipeline pair into slot 0..7 (slot, size>=56, blob) -> 0x300C",
"161": "set overlay image (w<=64, h<=64, RGBA4444 pixels) -- uploads into texture name 64",
"162": "arm overlay quad of size (w,h), ortho 320x240, 16.16 fixed vertices",
"163": "disarm overlay",
"164": "set render-server image (use, void* image, int size) -> 0/1"
```

### 12.10 What an emulator has to do to get Lost's frames to appear

In dependency order:

1. **Deliver `rserver.bin`.** Lost async-reads it into a 512 000-byte buffer and blocks its whole
   renderer on the completion callback. Until the callback fires and the state byte becomes 2, it
   loops presenting an empty frame. The file is 105 020 bytes and is present in `Games_RO/1B200`.
2. **Implement `#164` as "remember this blob"** and **`#152`/`#153` as a context reset**, both
   returning 1 (`#152` must also write `*outA = 1`, `*outB = 2`, though Lost discards them). The
   `#153; #164; #152` sequence destroys all textures and resets GL state — the emulator must be
   prepared for the game to re-upload everything afterwards, and must not fail a re-bind of a name
   it thinks it already knows.
3. **Accept `0x84F5` everywhere `0x0DE1` is accepted**, in `glBindTexture`, `glTexImage2D`,
   `glTexSubImage2D` and friends, and keep **one binding per texture unit**, not one per target.
   Lost's own wrapper rewrites every `GL_TEXTURE_2D` to `0x84F5` before the call, so a
   `0x0DE1`-only emulator drops 100 % of its texture traffic.
4. **Fix `glTexImage2D` for `GL_LUMINANCE_ALPHA`/`GL_UNSIGNED_BYTE`:** accept it, read
   `2*width*height` bytes with no row padding and no `GL_UNPACK_ALIGNMENT`, and expand as
   `(L, L, L, A)`. Same for `GL_LUMINANCE` (1 byte, `(L,L,L,255)`) and `GL_ALPHA` (1 byte,
   `(0,0,0,A)`), which Lost's font path may also use. NPOT sizes must be allowed.
5. **Decode vertex attributes as `GL_FIXED` (`0x140C`), 16.16.** Lost's positions, texcoords and
   colours all arrive this way. Uniform matrices are IEEE floats.
6. **Implement `#159` as a state write**, not a no-op that returns 0 — Lost's wrapper stores the
   value it passes in and uses the *return* as the previous index to restore. Returning 1 is enough
   for the ABI; the index itself has to be honoured as "which shader is active" if the emulator
   wants correct colouring. Indices Lost uses are 1, 2, 3, 14, 18, 39 plus data-driven values, all
   < 41; slots 41–48 are never loaded, so `#160` can stay unimplemented for this game.
7. **`glDrawArrays` mode 7 is a quad list** (§5), 4 vertices per quad, and that is the mode `#157`
   uses too.

Once the game is past step 1, the frames arrive through `#137` + `#40` + `#37` at the nine call
sites listed in 12.1. **There is nothing else to hook.**

### 12.11 What is not established

- **What the 50 `#159` pipelines actually compute.** The program objects live at fixed addresses
  in `0x108000a0..0x1081cf0c`, past the end of the `osos` image, so their contents are not in any
  file we have. Only their *identity* (which index selects which pair, and that index 49 aliases
  index 13) is measured. A faithful emulator will have to infer each index's meaning from how Lost
  and Minigolf set uniforms and attributes around it.
- **`#152`'s two output constants (1 and 2).** Hard-coded, and Lost discards them.
- **The `L,A` byte order inside a `GL_LUMINANCE_ALPHA` texel.** Measured: two bytes per texel,
  no padding, forwarded verbatim. The order is the GL-spec order by inference only, because the
  consumer is `rserver.bin`.
- **`#154` and `#158`.** `#154` clamps an argument to `[0,6]` and emits a packet; `#158` dispatches
  on a private enum block at `0x3f000..0x3f005`. Neither is reached by Lost, and neither was chased.
- **Why the emulator currently stops before `#137`.** The static reading says the draw path is
  ordinary and unconditional once the batch is non-empty (`0x50fc: ldr r0,[r0,#4]; ble return`) and
  once `[r9+8] == 2`. The two candidate causes are the `rserver.bin` read never completing and the
  texture uploads being dropped, and this section cannot choose between them without running it.
- Nothing in this section was executed. It is static reading of `osos.bin` and
  `Lost_1_1_2917525.bin`, cross-checked in both directions — Apple's `#161` calling Apple's `#99`
  pinned the `glTexImage2D` argument slots, and `#160`'s cache invalidation at `+0x148` pinned
  `#159`'s slot mapping — but never against a device or emulator.

### 12a. Where Lost actually stops — measured by walking the live call chain

§12 established that Lost draws with ordinary `#37 glDrawArrays` from nine call sites behind the
veneer at `0x18007340` (verified: `b 0xf8`, the `#37` thunk). None of those nine ever executes, and
neither does any of their callers. The whole rendering tree is dead.

What *is* alive is one chain, found by walking upward from the only live `glTexImage2D` caller:

```
vector[4] 0x1803d4f4
  -> 0x1803d698  bl 0x18006300     (inside the ctx[0]==0 arm)
     -> 0x18006300 -> 0x18035f30 -> 0x1800469c / 0x18004790 -> 0x18007438 -> #99
```

`0x18006300` is a small overlay renderer: two layer setups (`0x4d44`/`0x4ce4`/`0x4d80` with
`0x3f800000` = 1.0f), an allocation, then `0x18035f30`, which uploads exactly two textures. Both
come from **fixed addresses inside the eApp image** — `0x1803f220` (122x10) and `0x1803eed8`
(42x10) — so they are static art, not rendered text. Decoded as LUMINANCE_ALPHA they are a font
strip (`0123456789` plus a few letters) and two solid bars. The game re-uploads them every frame
and never draws them.

So Lost is sitting in one state, rendering an overlay, and never entering its renderer.

**The frame reason byte is not the cause.** RetailOS's pump writes `[ctx+0x00] = 5 or 4` before
every call, and we set it only at startup, after which Lost overwrites it from its own state at
`0x1803d844` and thereafter alternates 0/1. Refreshing it per frame was the obvious fix and it is
**wrong**: forcing any of 1, 2, 3, 4, 5 or 6 makes the game do strictly *less* work (≈2.3M
instructions per 347 frames against ≈3.8M when left alone), and the Audio, Metadata and texture
traffic disappear entirely. Reason 5 in particular looks like a suspend code. The `--frame-reason=N`
flag is kept as a diagnostic, off by default, so this does not get re-tried blind.

Both arms of the `ctx[0]` test do run — `0x1803d65c` and `0x1803d6a4` alternate frame by frame —
so the game is not stuck on that branch either. The open question is what advances it out of this
state; the `[r9+7]` action dispatch at `0x1803d7e0` (cases 2, 3, 4) and `bl 0x18006378` /
`0x1800822c` / `0x18007798` in the `[r9]==0` arm at `0x1803d81c` are the next places to look.

## 13. Bejeweled, booted — and five things it corrected

Bejeweled now boots to its menu and plays. Getting there fixed five things, three of which were
**wrong for every title**, not just this one.

### 13.1 Texture coordinates are texels, always

The renderer read `GL_TEXTURE_2D` (`0x0DE1`) as "normalised 0..1" and everything else as texels,
which is desktop GL's rule. It is not this driver's. Three independent measurements:

* Minigolf binds **only** `0x84F5`, so the normalising branch never ran for the title it was
  written for — its backgrounds were fixed by the attribute-1 colour/uv disambiguation (§5).
* Bejeweled binds `0x0DE1` and supplies uv of `1..154` against a 512-wide texture. Scaling those
  by the texture size ran every sample off the edge: its screen was uniformly white.
* Lost's own mapper at `0x18008634` rewrites `0x0DE1 -> 0x84F5` before every call.

### 13.2 The open mode: 0 reads, 1 writes

The low byte of `AsyncFileIO #3`'s first argument. Minigolf opens every asset with 0; Bejeweled
opens `Prefs` with **1** and, getting a miss, never proceeds. A write-mode open must be allowed to
create the file (`--allow-creates`). This is almost certainly the same mechanism behind Minigolf
never writing a save.

### 13.3 Load-on-open is not a Lost quirk

Bejeweled reaches `AsyncFileIO #3` and **never `#2`** — it hands the open a destination buffer and
expects the data to be there on completion, exactly as Lost does. With `--load-on-open` it goes
from 210 quads to **686 000**. Two of the three titles that get this far want it, so it is the
common convention and the read path is the exception.

### 13.4 Bit 30 of the input event word is EVENT PRESENT

`0x180209f8`: `tst r0,#0x40000000 / beq` — Bejeweled discards the whole event word unless that bit
is set, so it saw no input at all. Minigolf reads the low byte regardless, which is why the
omission survived this long.

### 13.5 Buttons arrive two different ways

Minigolf reads **flag bits** in a word of its own (§8). Bejeweled decodes **event-list nodes** at
`0x18013ebc`, and is stricter about them: the node's state byte must be **2** for a press (1 is the
release, handled separately), and the type byte selects the bit through a jump table:

| node type | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| bit | `0x10` | `0x01` | `0x02` | `0x04` | `0x08` |

So Select is **type 2, state 2**. `post_event` had state hardcoded to 1, which is what Apple's own
`InputEvents #1` decoder wants — but not what this game's does. `--event-buttons` selects the node
route; without it Bejeweled navigates by wheel but nothing can be chosen.

### 13.6 Two smaller ones

Recycled heap blocks are now zeroed — a stale id field at `+0x10` sent Bejeweled into the `b .`
assertion at `0x18014aac`, whose release path requires an id below 64. And `--flip-y` exists
because Bejeweled works in top-left screen coordinates: its whole menu rendered upside down.
Its `glUniformMatrix4fv` is the **identity**, so there is nothing in-band to derive this from;
the matrix check is kept for titles that set a real `ortho`, where a negative element 5 says the
same thing.

**Working command:**

```
play .../Bejeweled_1_1_2563296.bin --async-files --allow-creates --load-on-open \
     --flip-y --event-buttons --budget=400000000
```

## 14. Zuma, playing — buttons can need BOTH mechanisms

Zuma boots to its menu and plays. It wanted the same options Bejeweled did (`--load-on-open`,
`--allow-creates`, `--flip-y`) plus one new fact.

§13.5 recorded two ways buttons arrive: Minigolf's **flag bits** and Bejeweled's **event-list
nodes**. Zuma needs **both at once**. With only the nodes, or only the flags word, its wheel
navigates the menu but nothing can be selected; with both, it starts a game.

Its poll at `0x1802e270` has the same `bic r0,r0,#0x60` flags shape as the other two (flags word at
`0x180b8f08`), but its decoder does not consume the flags there — `0x1801493c` merely *stores* the
flags and the list head into a global at `0x180bfa98`, and `0x180135e0` later snapshots offsets
0/4/8/0x18 of that global into a caller's struct. So the title reads the pair together, and
supplying one without the other leaves it half-informed.

The viewer now delivers both whenever a flags word and `--event-buttons` are given together, which
is a superset of what Minigolf and Bejeweled each need and leaves both unchanged.

**Working command:**

```
play .../Zuma_1_1_2563298.bin --async-files --allow-creates --load-on-open \
     --flip-y --event-buttons --flags-addr=0x180b8f08 --budget=400000000
```

### 14.1 Pac-Man: where it stops

Pac-Man renders its loading screen correctly and never leaves it. Its sound loader iterates
`index = (C - B) / 2` from counters at `[obj+0x198c]`/`[obj+0x1998]`; measured, **C=14, B=13**, so
the delta is odd and it takes the polling branch at `0x180139b8` forever. That is why every open is
`/audio/extra life.wav` — entry 0 of the 16-name table at `0x1802c7fc`, with the index frozen.

The poll waits for `[obj+0x1d0]` to reach 1; watched, it cycles `0 -> 2 -> 0 -> 2`. State 1 is
written only by `0x18008a90` when its status argument is 0, and that arrives as **5**.

Ruled out by measurement, not assumption: the slot guard at `0x18004374` (the slot is free when
checked), the channel-enable table at `0x1802c5f8` (channel 0 *is* enabled, `0 -> 1`), the request
field `[req+0x2c]` the completion copies (correctly `-1`), and the validator `0x1801aed8` (returns
1 = success). The remaining lead is `0x1801b1d0`, which calls the entry scanner with **r0 = 0** —
a null object — and the scanner then reads a non-zero byte where a null-derived read should give 0.

## 15. Five more OpenGLES entries

Targets: **`#45`, `#148`, `#158`, `#165`, `#167`**. All `osos` addresses are file offsets
(`VA = 0x10000000 + N`). Game addresses are file offsets in the eApp (`VA = 0x18000000 + N`).
Everything here is static reading of `osos.bin`, `Zuma_1_1_2563298.bin` and
`Pacman_1_1_2563976.bin`; nothing was executed. Where the runtime traces in the emulator and the
static reading disagree, that is called out rather than smoothed over.

**Headline result, up front:** `#148` is a **per-draw RGBA modulate colour in 16.16 fixed point**,
and it is the single most important of the five. `#165` and `#167` are **not driver entry points at
all** — they are pure `mat4` math helpers that write 64 bytes of the caller's memory and touch
nothing else. **None of the five can select which texture a draw samples.**

### 15.1 The names are in the image

`osos` carries its own GL function-name table, used by the argument validators to print
`"Error in side %s\n"`. That settles three of the five by direct quotation rather than by
inference. The table runs `0x2a9420..0x2a9958`:

```
0x2a956d  'glGenBuffers'
0x2a957a  'glGenTextures'          <- quoted by #45  (literal at 0x26e69c)
0x2a97ca  'glUniform4fv'
0x2a97d7  'glUniform4iv'
0x2a97e4  'glUniform4xvAPPLE'      <- quoted by #148 (literal at 0x2717f4)
0x2a982f  'glUniformMatrix4xvAPPLE'
```

The extension string at `0x2a9958` is worth recording in full, because three separate puzzles in
this document fall out of it:

```
GL_APPLE_fence GL_APPLE_ES_fixed_point GL_APPLE_ES_quads GL_ARB_texture_rectangle
GL_OES_compressed_paletted_texture GL_OES_mapbuffer
```

* `GL_APPLE_ES_fixed_point` — the `…x…APPLE` suffix means **`GLfixed`, 16.16**, not float. That is
  what the `x` in `glUniform4xvAPPLE` denotes.
* `GL_ARB_texture_rectangle` — `0x84F5` is `GL_TEXTURE_RECTANGLE_ARB`, which is why §13.1's
  "texture coordinates are texels, always" is correct, and why NPOT sizes are accepted (§12.8).
* `GL_APPLE_ES_quads` — `glDrawArrays` mode 7 is a quad list (§5, §12.7).

### 15.2 `#45` — `glGenTextures`, and names start at **1**

`0x0026e6b8`. Two arguments, returns `void`.

```c
void glGenTextures(GLsizei n, GLuint *textures);
```

```asm
26e6b8:  cmp   r0,#0
26e6bc:  ldr   r3,[pc,#68]      ; 0x1084bbc4  -- the GL context
26e6c0:  movge r2,#0            ; i = 0
26e6c8:  bge   0x26e6fc
26e6cc:  ...                    ; n < 0 -> ctx[0x88] = 0x0501, printf("Error in side %s", "glGenTextures"), 0x7ca24
26e6e4:  ldr   ip,[r3,#0x270]   ; the texture-name counter
26e6e8:  str   ip,[r1,r2,lsl #2]; textures[i] = counter
26e6ec:  ldr   ip,[r3,#0x270]
26e6f0:  add   r2,r2,#1
26e6f4:  add   ip,ip,#1
26e6f8:  str   ip,[r3,#0x270]   ; counter++
26e6fc:  cmp   r2,r0
26e700:  blt   0x26e6e4
```

That is the whole function. **No object is created**; the call is a name allocator and nothing
more. The name only becomes a real texture when `#4 glBindTexture` first sees it (§12.1: the bind
stores the name in `ctx[0x94 + 4*activeUnit]`, one slot per unit, no per-target separation).

Three measured facts an emulator has to match:

* **The counter is `ctx + 0x270`, and `0xce0fc` initialises it to 1** (§12.2:
  `[+0x26c]=1`, `[+0x270]=1`). So **the first name ever returned is `1`, and `0` is never
  handed out.** `0` is the driver's "nothing bound" value, restored by the reset and saved/restored
  by `#157`.
* **Textures and buffers have separate counters.** `#44 glGenBuffers` (`0x26e644`) is the same
  function against `ctx + 0x26c`:

  ```asm
  26e670:  ldr ip,[r3,#0x26c]
  26e674:  str ip,[r1,r2,lsl #2]
  26e684:  str ip,[r3,#0x26c]
  ```

  so buffer name 1 and texture name 1 coexist.
* **Names are never recycled.** `#28/#29/#30 glDeleteTextures` cannot return a name to this
  counter; it only ever increments.

Zuma calls it from exactly two sites, `0x120f0` and `0x12150`, both `n = 1`. The first is the one
that matters here:

```asm
120e0:  push {r4,r5,lr}
120e4:  ldr  r1,[pc,#188]      ; &0x180b7b34
120ec:  mov  r0,#1
120f0:  bl   0x118             ; #45 glGenTextures(1, &0x180b7b34)
120fc:  mov  r5,#0x0DE1
12100:  ldr  r1,[r4,#12]       ; r4 = 0x180b7b28, so [r4+12] IS 0x180b7b34
12108:  bl   0x74              ; #4  glBindTexture(GL_TEXTURE_2D, thatName)
1210c:  mvn  r0,#0             ; the pixel: 0xFFFFFFFF
12118:  str  r0,[sp,#24]
12120:  add  r1,r1,#0x1900     ; 0x1907 GL_RGB
12124:  add  r2,r2,#0x8300     ; 0x8363 GL_UNSIGNED_SHORT_5_6_5
1212c:  stmib sp,{r0,r1,r2,r3} ; border=0, format=GL_RGB, type=5_6_5, pixels=&0xFFFFFFFF
12130:  mov  r3,#1             ; width  = 1
12140:  str  r3,[sp]           ; height = 1
12144:  bl   0x1f0             ; #99 glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,1,1,0,GL_RGB,5_6_5,ptr)
```

**This is the 1×1 white texture the emulator is seeing.** It is created deliberately, first, by
Zuma, and it is the texture every flat-colour quad binds. It is not a fallback and not a bug. The
colour of those quads does not come from it — it comes from `#148`.

Two consequences worth writing down:

1. If the emulator's `glGenTextures` starts at 0, its first name collides with the driver's
   "unbound" value. Everything still works internally, but a trace that reads *"drawn with texture
   name 0"* is ambiguous between "the white texture" and "nothing bound", which is exactly the
   ambiguity in the current Zuma report. **Start the counter at 1** and the two cases separate.
2. Zuma's second site assumes small names: `0x1215c: cmp r1,#64; addcc r0,r2,r1,lsl #3` — a
   64-entry side table indexed by texture name. A generator that hands out large or sparse names
   will silently fall off the `cc` branch.

### 15.3 `#148` — `glUniform4xvAPPLE`: the per-draw modulate colour

`0x0027171c`. Three arguments, returns `void`. The fourth register is **not read**.

```c
void glUniform4xvAPPLE(GLint location, GLsizei count, const GLfixed *value);   // 4*count 16.16 values
```

```asm
27171c:  push {r4,r5,r6,r7,r8,lr}
271720:  cmn  r0,#1
271728:  mov  r8,r1            ; count
27172c:  mov  r6,r2            ; value
271730:  mov  r4,r0            ; location
271734:  beq  0x2717e4         ; location == -1 -> return, silently
271738:  cmp  r8,#0
27173c:  bge  0x27175c
                               ; count < 0 -> ctx[0x88]=0x0501, "Error in side glUniform4xvAPPLE"
27175c:  add  r0,sp,#8 ; stm r0,{0,0,0,0}      ; 16-byte packet header, zeroed
271774:  ldr  r0,[sp,#8]
271778:  cmp  r4,#4
27177c:  bic  r0,r0,#31        ; opcode field (low 5 bits) = 0
271780:  str  r0,[sp,#8]
271788:  ldrcs r7,[pc,#124]    ; location >= 4  ->  r7 = 0x00000101
27178c:  bic  r0,r0,#3
271790:  orr  r0,r0,#1         ; hdr1 low half = 1 register per packet
271798:  mov  r0,#16
27179c:  subcs r4,r4,#4        ; location >= 4  ->  location -= 4
2717a0:  movcc r7,#1           ; location <  4  ->  r7 = 1
2717a4:  str  r0,[sp,#16]      ; payload length = 16 bytes
2717ac:  add  r0,r7,r4
2717b0:  strh r0,[sp,#14]      ; hdr1 high half = the hardware register tag
2717c0:  str  r6,[sp,#4]       ; payload pointer = value
2717c8:  ldr  r0,[pc,#64]      ; 0x1080009c -- the command stream
2717cc:  bl   0x27f3b8         ; emit
2717d0:  add  r6,r6,#16        ; next vec4
2717d4:  add  r4,r4,#1         ; next register
2717dc:  cmp  r5,r8 ; blt loop
```

Measured properties:

* **16 bytes per element, `count` elements**, `value` advanced by 16 each time. It is a `vec4`
  setter, and the driver stub does **no conversion** — the four words go into the ring verbatim.
* **`location == -1` is a silent no-op**, the standard GL contract for an unresolved uniform.
* **`r3` is never read.** The emulator's trace notes `r3 = 0` or `r3 = 0x10000` at the call; that is
  caller garbage, not an argument. (It is a plausible-looking value because everything in this
  layer *is* 16.16.)

**The payload is 16.16 fixed, positively, not by inference.** `#120 glUniform4fv` (`0x271334`) is
the same function with a conversion in front of it:

```asm
2713c4:  ldr r0,[r4]        ; an IEEE float
2713c8:  bl  0x2a8418       ; float -> double
2713cc:  mov r2,#16
2713d0:  bl  0x2a929c       ; ldexp(x, 16)
2713d4:  bl  0x2a76dc       ; -> integer
2713d8:  str r0,[sp,#24]    ; ...and the same for [r4+4], [r4+8], [r4+12]
271444:  ldr r0,[pc,#64]    ; 0x1080009c
271448:  bl  0x27f3b8       ; identical packet, identical tag arithmetic
```

i.e. `glUniform4fv` = `glUniform4xvAPPLE(location, count, (GLfixed)(f * 65536))`. **The hardware
constant registers are 16.16 fixed point.** `#125 glUniformMatrix4fv` (`0x271e0c`) does the same
thing for 16 floats.

#### The register-tag map, and why `location = 4` is the colour

The tag arithmetic is shared by `#120`, `#125` and `#148`:

| GL `location` | emitted tag |
|--:|---|
| 0, 1, 2, 3 | `0x0001, 0x0002, 0x0003, 0x0004` |
| 4, 5, 6, … | `0x0101, 0x0102, 0x0103, …` |

So there are **two constant banks**: bank 0 at tags `0x0001+` and bank 1 at tags `0x0101+`.

`#125 glUniformMatrix4fv` proves that a `mat4` consumes **four consecutive locations**, one per
column, incrementing the tag:

```asm
272044:  strh r8,[sp,#14]      ; tag = base
272050:  bl   0x27f3b8         ; payload sl+0
272054:  add  r0,r8,#1 ; strh r0,[sp,#14]
272064:  str  r3,[sp,#4]       ; payload sl+16
272078:  bl   0x27f3b8
27207c:  add  r0,r8,#2         ; payload sl+32
2720a4:  add  r0,r8,#3         ; payload sl+48
2720d4:  add  r5,r5,#4         ; location += 4 for the next matrix in the array
```

Both games call `glUniformMatrix4fv(0, 1, 0, mvp)`. That occupies locations 0–3 → tags 1–4 → the
whole of bank 0. **`location = 4` is therefore the first location past the matrix, and it lands in a
different register bank.** A vertex bank holding one 4×4 matrix, and a second bank whose first
register is a constant colour, is exactly the shape of this hardware.

#### What Zuma puts there — measured, and it is a colour

Zuma's per-draw block at `0x22fe8` builds the four words immediately before the call, out of an
**RGB565 word** and an 8-bit alpha:

```asm
22fe8:  ldrh r0,[r5,#20]       ; packed RGB565
22ff4:  and  r0,r0,#0xF800     ; red   field  (r << 11)
22ff8:  add  r0,r0,#0x800
22ffc:  str  r0,[sp,#8]        ; R
23000:  ldrh r0,[r5,#20]
23004:  and  r0,r0,#0x7E0      ; green field  (g << 5)
23008:  add  r0,r1,r0,lsl #5   ; r1 = 0x400
2300c:  str  r0,[sp,#12]       ; G
23010:  ldrh r0,[r5,#20]
23018:  and  r0,r0,#0x1F       ; blue  field
2301c:  add  r0,r1,r0,lsl #11  ; r1 = 0x800
23020:  str  r0,[sp,#16]       ; B
23024:  ldrh r0,[r5,#18]       ; alpha, 0..256
2302c:  lsl  r0,r0,#16
23038:  asr  r0,r0,#8          ; alpha << 8
2303c:  str  r0,[sp,#20]       ; A
```

Check the arithmetic against 16.16 (`1.0 == 0x10000`):

| channel | expression | at max | value |
|---|---|--:|---|
| R | `(r<<11) + 0x800` | r = 31 | `0xF800 + 0x800 = 0x10000` = 1.0 |
| G | `0x400 + (g<<10)` | g = 63 | `0xFC00 + 0x400 = 0x10000` = 1.0 |
| B | `0x800 + (b<<11)` | b = 31 | `0xF800 + 0x800 = 0x10000` = 1.0 |
| A | `a << 8` | a = 256 | `0x10000` = 1.0 |

Every channel saturates at exactly 1.0, and each is the *centre* of its 565 bucket
(`(r + 0.5)/32`, `(g + 0.5)/64`, `(b + 0.5)/32`). **This is an RGBA colour, converted to 16.16, and
nothing else.**

The full draw block, `0x2307c..0x2312c`, is the whole story in one place:

```asm
2307c:  blne 0x2e0            ; #159 glSetPipelineAPPLE(index)  -- only when the index changed
23080:  mov  r0,#0x0DE1
23084:  ldr  r1,[r4,#0x24]
2308c:  bl   0x74             ; #4   glBindTexture(GL_TEXTURE_2D, tex)
230b4:  bl   0x288            ; #137 glVertexAttribPointer(0, 4, 0x140C GL_FIXED, 0, 0, sp+24)
230bc:  bl   0x104            ; #40  glEnableVertexAttribArray(0)
230dc:  bl   0x288            ; #137 glVertexAttribPointer(1, 2, 0x140C GL_FIXED, 0, 0, sp+88)
230e4:  bl   0x104            ; #40  glEnableVertexAttribArray(1)
230e8:  add  r2,sp,#8
230ec:  mov  r1,#1
230f0:  mov  r0,#4
230f4:  bl   0x2b4            ; #148 glUniform4xvAPPLE(4, 1, {R,G,B,A} 16.16)
23100:  ldr  r0,[0x180b663c]  ; current matrix
23108:  bl   0x2f8            ; #165 loadIdentity(m)
2310c:  ldr  r3,=0x180b6640   ; the projection matrix
2311c:  bl   0x258            ; #125 glUniformMatrix4fv(0, 1, 0, m)
23120:  mov  r2,#4 ; mov r1,#0 ; mov r0,#7
2312c:  bl   0xf8             ; #37  glDrawArrays(7 = quad list, 0, 4)
```

Only attributes **0 (position, 4×fixed)** and **1 (texcoord, 2×fixed)** are supplied. There is no
colour array. The colour of this quad exists **only** in uniform location 4.

The other Zuma site, `0x4b34`, confirms the layout from the opposite direction — it copies a
four-word constant straight in:

```asm
4b34:  ldr r1,[pc,#292]       ; -> 0x1802fc70
4b40:  ldm r1,{r2,r3,ip,lr}
4b48:  stm r0,{r2,r3,ip,lr}   ; sp+4 .. sp+16
4b50:  mov r0,#4 ; mov r1,#1 ; add r2,sp,#4
4b58:  bl  0x2b4              ; #148
```

and `0x1802fc70` holds `{0x10000, 0x10000, 0x10000, 0x10000}` — **opaque white, i.e. "no
modulation"**. A game that sometimes ships literal white and sometimes ships a 565-derived tint is
a game that is using this as a modulate colour, not as anything else.

#### What this explains

* **Zuma's white screen.** The full-screen quad bound to the 1×1 white texture is a *flat colour
  fill*: white texel × uniform-4 colour. An emulator that ignores uniform 4 renders it white and
  opaque. Every solid rectangle Zuma draws — background wash, panels, bars, fades — arrives through
  this same path, so ignoring uniform 4 turns all of them white.
* **Pac-Man's power pellets.** Any renderer that drops a per-draw modulate colour tints white dots
  wrong. See 15.7 for the caveat: the Pac-Man binary in `Games_RO/AAAAA` does **not** call `#148`,
  so for that title this is a mechanism, not a proven cause.
* It does **not** explain the missing 322×222 background. See 15.6.

#### What an emulator must do

```
on #148(location, count, value):
    if location == -1: return
    if count < 0: set GL_INVALID_VALUE (0x0501); return
    for i in 0..count-1:
        reg = (location + i < 4) ? (1 + location + i) : (0x101 + location + i - 4)
        store the 4 words at value + 16*i into constant register `reg`, as raw 16.16
```

and at draw time: `fragment = texel(sampled) * constant_register[0x101]`, with the four components
read as `R,G,B,A = reg[0..3] / 65536.0`. Clamp to `[0,1]`; the driver does not clamp, and Zuma's
own arithmetic never exceeds 1.0. Do the same for `#120 glUniform4fv` after multiplying by 65536,
and treat `#147` (§12.7, the scalar form) as writing the same register from `r1..r4`.

The colour must be **latched per draw**, not per frame: Zuma re-sets it before every
`glDrawArrays`.

### 15.4 `#165` and `#167` are a matrix library, not driver calls

This is the correction to §12.9's blanket "`#152..#178` are not GL entry points at all". That is
right about `#152..#164`, but `#165..#178` are a **`mat4` math library** — the fixed-function
matrix helpers that GL ES 2.0 deleted, shipped in the same interface so games do not have to carry
their own. They read and write **only the caller's 64-byte matrix**. They emit no packets, touch no
context, and have no side effects of any kind.

#### `#165` — load identity (float)

`0x002734d4`, one argument, returns `void`.

```c
void glLoadIdentityAPPLE(GLfloat m[16]);
```

```asm
2734d4:  mov r1,#0
2734d8:  str r1,[r0,#56] ... str r1,[r0,#4]     ; twelve zero stores: +4 +8 +c +10 +18 +1c +20 +24 +2c +30 +34 +38
273508:  mov r1,#0x3f800000                      ; 1.0f
27350c:  str r1,[r0,#60]
273510:  str r1,[r0,#40]
273514:  str r1,[r0,#20]
273518:  str r1,[r0]
27351c:  bx  lr
```

Twenty instructions, one `bx lr`, no calls. **`r1`, `r2` and `r3` are dead on entry** — `r1` is
overwritten by the first instruction. The emulator's trace of `r0 = 0x180b6680, r1 = 0x180b6630,
r2 = <ptr>, r3 = 0`, and the same call "also with `r2 = 320.0f, r3 = 240.0f`", is leftover register
content from the caller; those two floats are Zuma's screen constants left in flight from the
neighbouring `#167` setup. **`#165` has exactly one argument.**

Its twin `#166` (`0x273520`) is byte-for-byte the same function with `mov r1,#0x10000` instead of
`mov r1,#0x3f800000` — **the 16.16 identity**, `glLoadIdentityxAPPLE`. That pairing is the cleanest
possible evidence for what this block is.

#### `#167` — `glOrtho` (float)

`0x0027356c`, **seven** arguments (four in registers, three on the stack), returns `void`.

```c
void glOrthoAPPLE(GLfloat m[16], GLfloat left, GLfloat right,
                  GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
```

The prologue pins the stack arguments exactly:

```asm
27356c:  push {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,sl,fp,lr}   ; 13 words = 52 bytes
273570:  sub  sp,sp,#4
273574:  add  fp,sp,#56          ; sp+56 == the caller's first stack argument
273578:  mov  r4,r0              ; dst
27357c:  ldr  r0,[sp,#12]        ; saved r2 = right
273580:  ldm  fp,{r9,sl,fp}      ; r9 = top, sl = zNear, fp = zFar
273584:  ldr  r1,[sp,#8]         ; saved r1 = left
273588:  mov  r5,r3              ; bottom
27358c:  bl   0x2a8f84           ; __aeabi_fsub  -> r6 = right - left
273594:  mov  r0,r9 ; mov r1,r5
27359c:  bl   0x2a8f84           ;               -> r7 = top - bottom
2735a4:  mov  r0,sl ; mov r1,fp
2735ac:  bl   0x2a8f84           ;               -> r8 = zNear - zFar
2735b4:  mov  r0,#0x3f800000 ; mov r1,r6
2735b8:  bl   0x2a85c4           ; __aeabi_fdiv  -> r6 = 1/(right-left)
                                 ;                  r7 = 1/(top-bottom), r8 = 1/(zNear-zFar)
2735e4:  mov  r0,#0 ; str r0,[r4,#44] ... [r4,#4]   ; zero the nine off-diagonal slots
27360c:  mov r0,r6 ; mov r1,#1 ; bl 0x2a9310        ; ldexp(x,1) = 2x
273618:  str r0,[r4]             ; m[0]  = 2/(right-left)
273628:  str r0,[r4,#20]         ; m[5]  = 2/(top-bottom)
273638:  str r0,[r4,#40]         ; m[10] = 2/(zNear-zFar)
27363c:  ldr r0,[sp,#12] ; ldr r1,[sp,#8] ; bl 0x2a847c   ; right + left
273648:  eor r0,r0,#0x80000000                            ; negate
273650:  bl  0x2a8ac4 ; str r0,[r4,#48]   ; m[12] = -(right+left)/(right-left)
273670:  str r0,[r4,#52]                  ; m[13] = -(top+bottom)/(top-bottom)
273688:  str r0,[r4,#56]                  ; m[14] =  (zFar+zNear)/(zNear-zFar)
27368c:  mov r0,#0x3f800000
273690:  str r0,[r4,#60]                  ; m[15] = 1.0f
```

`0x2a847c`, `0x2a85c4`, `0x2a8ac4`, `0x2a8f84`, `0x2a9310` are `__aeabi_fadd`, `fdiv`, `fmul`,
`fsub` and `ldexpf` — pure, register-only, no globals (`0x2a8f84` opens
`eor r2,r0,r0,lsl #1; tst r2,#0x7f000000`, the standard soft-float NaN screen). So `#167` writes
64 bytes at `r0` and nothing else, ever.

That is textbook `glOrtho`, **column-major with the translation in `m[12..14]`**.

Both games confirm the slot order independently. Zuma, `0x4008`:

```asm
4008:  mov r0,#1 ; bl 0x5fe4        ; the game's own "matrix mode = projection"
4014:  ldr r0,[0x180b663c]
4018:  bl  0x2f8                    ; #165 loadIdentity(m)
401c:  mov r0,#0x4A480000 ; mvn r1,#15 ; bl 0x2f3e4   ; ldexp( 3276800.0, -16) =   50.0
4030:  mov r0,#0xCA480000 ; ...                       ;                          -50.0
4054:  mov r0,#0x4B700000 ; ...                       ; ldexp(15728640.0, -16) =  240.0
4068:  mov r0,#0x4BA00000 ; ...                       ; ldexp(20971520.0, -16) =  320.0
407c:  ldr r0,=0x180b6640           ; dst
4080:  mov r3,fp                    ; bottom = 240.0f
4084:  mov r1,sl                    ; left   =   0.0f
4088:  str sl,[sp]                  ; top    =   0.0f
408c:  str r8,[sp,#8]               ; zFar   =  50.0f
4090:  str r9,[sp,#4]               ; zNear  = -50.0f
4094:  bl  0x300                    ; #167 ortho(m, 0, 320, 240, 0, -50, 50)
4098:  mov r0,#2 ; bl 0x5fe4        ; "matrix mode = modelview"
40a4:  bl  0x2f8                    ; #165 loadIdentity
```

Pac-Man, `0x4394`, with the same idiom and the opposite Y convention:

```asm
4394:  push {r1,r2,r3,lr}           ; reserves the three stack-argument slots
4398:  mov r3,#0x3f800000           ;  1.0f
439c:  orr r2,r3,r3,lsl #8          ; 0xbf800000 = -1.0f
43a0:  add r1,r3,#0x3f00000         ; 0x43700000 = 240.0f
43a4:  stm sp,{r1,r2,r3}            ; top = 240.0f, zNear = -1.0f, zFar = 1.0f
43a8:  mov r3,#0                    ; bottom = 0.0f
43ac:  add r2,r1,#0x300000          ; 0x43a00000 = 320.0f = right
43b0:  mov r1,#0                    ; left = 0.0f
43b4:  ldr r0,=0x1802c6a8
43b8:  bl  0x300                    ; #167 ortho(m, 0, 320, 0, 240, -1, 1)
```

The emulator's trace — `r0 = 0x180b6640, r1 = 0, r2 = 0x43a00000, r3 = 0x43700000` — is Zuma's
`0x4094` site exactly: `m = 0x180b6640`, `left = 0`, `right = 320.0f`, `bottom = 240.0f`. `0x180b6640`
is the literal at Zuma `0x41cc` and `0x23158`, i.e. its projection matrix; `0x180b663c` is the
pointer to the current matrix, which is what `#165` receives.

This also confirms §12.6's reading of `#162`'s call — `ortho(dst, 0, 320.0f, 0, 240.0f, -1.0f,
1.0f)` — and, incidentally, that **the framebuffer is 320×240** and that these matrices are IEEE
floats even though vertex attributes are `GL_FIXED`.

#### The rest of the block, for the record

Same shape, same "writes only the caller's matrix" property. Named from their arithmetic:

| # | addr | what it is |
|--:|---|---|
| 165 | `0x2734d4` | `loadIdentity` (float) |
| 166 | `0x273520` | `loadIdentity` (16.16) |
| 167 | `0x27356c` | `ortho` (float) |
| 168 | `0x27369c` | `perspective(fovy, aspect, near, far)` — `ldexp(fovy,-1)`, ×π/180, `tan` at `0x7d5c8` |
| 169 | `0x275004` | `translatef` — `m[12] += m[0]*x + m[4]*y + m[8]*z` |
| 170 | `0x27513c` | `translatex` — same, with `smull`/`lsr #16`/`orr lsl #16` fixed multiplies |
| 171 | `0x274dfc` | `scalef` — `m[0]*=x; m[4]*=y; m[8]*=z; m[1]*=x; …` |
| 172 | `0x274ed0` | `scalex` |
| 173 | `0x27434c` | `rotatef` — `sin` at `0x7dd5c`, `cos` at `0x7d69c`, a π/180 literal at `0x2748b8` |
| 174 | `0x2748bc` | `rotatex` |
| 175 | `0x273db8` | `multMatrixf(dst, a, b)` — three pointers, float |
| 176 | `0x2737a4` | `multMatrixx(dst, a, b)` — the `smull` version |
| 178 | `0x2732c4` | float array → 16.16 array (`0x2a8418` → `ldexp(·,16)` → `0x2a76dc`, per element) |

`#177` (`0x273454`) is the one member of this address range that is *not* math: it emits an
opcode-10 packet on `0x1080009c` with a 16-bit tag from `r1`, a second from `r2`, a length from
`r3` and the payload at `r0` — a raw block upload. It is not called by any of the four games read
here.

Zuma uses `#169`, `#171`, `#173` and `#175` (14, 5, 7 and 7 call sites), which is precisely a game
driving a translate/scale/rotate/multiply matrix stack. **`#165`/`#167` are two entries of that
library and carry no driver state.**

### 15.5 `#158` — a private six-value enable/disable

`0x0026b3f0`. Two arguments, returns `0x3000` (accepted) or `0x300C` (bad enum).

```c
int glEnableAPPLEPrivate(GLenum pname /* 0x3F000..0x3F005 */, GLint enable);
```

```asm
26b3f0:  push {r4,lr}
26b3f4:  subs lr,r1,#0            ; flags come from the SECOND argument
26b400:  ... stm r4,{0,0,0,0}     ; 16-byte packet header, zeroed
26b414:  sub  r1,r0,#0x3f000
26b418:  movgt r0,#1              ; r0 = (enable > 0) ? 1 : 0
26b41c:  movle r0,#0
26b420:  cmp  r1,#5
26b424:  addls pc,pc,r1,lsl #2
26b428:  b    0x26b570            ; default -> return 0x300C
26b42c:  b 0x26b444   ; 0x3F000 -> selector 1
26b430:  b 0x26b484   ; 0x3F001 -> selector 2
26b434:  b 0x26b49c   ; 0x3F002 -> selector 3
26b438:  b 0x26b4b4   ; 0x3F003 -> selector 4  (+ 0x27ed64)
26b43c:  b 0x26b500   ; 0x3F004 -> selector 5  (+ 0x27ed40)
26b440:  b 0x26b554   ; 0x3F005 -> selector 6
26b444:  ldr r1,[sp,#8] ; bic r1,r1,#31 ; orr r1,r1,#11   ; opcode 11
26b45c:  str r0,[sp,#16]          ; the boolean travels in the third header word
26b478:  ldr r0,=0x1080009c ; bl 0x27f3b8                 ; emit, no payload
26b548:  mov r0,#0x3000 ; pop
```

So: **the enum picks a selector 1..6; the second argument is reduced to a boolean (`> 0`); one
payload-less opcode-11 packet is emitted; the return is `0x3000`.** An out-of-range enum emits
nothing and returns `0x300C` — the same "rejected" code `#160` returns (§12.6).

Two of the six also touch the CPU-side ring-manager flag word at `0x10871448 + 12`:

```asm
27ed64:  ldr r3,=0x10871448 ; mov r2,#0x200
27ed6c:  ldr r1,[r3,#12]
27ed70:  and r0,r2,r0,lsl #9      ; bit 9   <- boolean          (0x3F003)
27ed7c:  orr r0,r1,r0 ; str r0,[r3,#12]

27ed40:  ... and r0,r2,r0,lsl #10 ; bit 10  <- boolean          (0x3F004)
```

The same word carries the ring's "buffer busy" bit 0 and the current-buffer index in bits 1–4
(`0x27ed88`), so bits 9 and 10 are ring/present policy flags. **What any of the six selectors
actually control is not established** — the consumer is `rserver.bin`, which is not disassembled
here.

Both games use only `0x3F001` (selector 2, no CPU-side flag), once each, from a
"the display object reached state 2" guard:

```asm
; Zuma 0x10074
10074:  ldr  r0,[r6,#12] ; cmp r0,#2 ; bne skip
10084:  str  r7,[0x180b6700]
10088:  mov  r0,#1 ; add r0,r0,#0x3f000     ; pname = 0x3F001
1008c:  ldr  r1,[r6,#24]                    ; the flag
10094:  bl   0x2dc                          ; #158

; Pac-Man 0x3e8c
3e8c:  ldr r0,[0x1802c744] ; cmp r0,#2 ; bne skip
3ea8:  ldr r1,[0x1802c750]                  ; the flag
3eb0:  ldr r0,=0x3F001
3eb4:  bl  0x2dc                            ; #158
```

**Emulator: accept `0x3F000..0x3F005`, store the boolean in a six-entry array, return `0x3000`;
return `0x300C` for anything else. Nothing in the draw path reads it.** Neither game's rendering
depends on the call succeeding, and Pac-Man's is on the far side of a state gate it may never
reach.

### 15.6 Can any of these five select a texture? **No.**

This was the most important question, and the answer is unambiguous.

* `#45` **allocates a name** and does not bind it, does not create an object, and does not touch
  `ctx[0x94]`.
* `#148` writes a constant register in bank 1. It cannot address a texture unit; there is no
  sampler-index path in it — the payload is 16 opaque bytes and the tag is `0x101 + (location-4)`.
* `#165`, `#167` write 64 bytes of the caller's own memory and call nothing but soft-float.
* `#158` emits a payload-less opcode-11 packet with a boolean.

**The only way to change which texture a draw samples in this interface is `#4 glBindTexture`,**
with `#0 glActiveTexture` choosing the unit (§12.1: one binding slot per unit at
`ctx[0x94 + 4*unit]`, no per-target separation, `0x0DE1` and `0x84F5` aliasing the same slot).
And **Zuma never calls `#0`** — its used-ordinal set is
`{4, 12, 13, 36, 37, 40, 45, 84, 99, 105, 125, 137, 148, 157, 158, 159, 165, 167, 169, 171, 173,
175}` — so it is single-unit throughout, and `#4` is the whole of its texture selection, from
seventeen call sites.

So for **Zuma's missing 322×222 background**, these five are not the answer, and the search has to
move:

* The bind is there in the binary — `0x4aa4`, `0x4b74`, `0x90a4`, `0x9404`, `0xed80`, `0x12108`,
  `0x16544`, `0x18014`, `0x1cf80`, `0x2308c`, `0x23650`, `0x2419c`, `0x2669c`, `0x269bc`,
  `0x26c3c`, `0x26ef0`, `0x2bca8`. Every one reads the name out of a struct field (`[r4,#0x24]`,
  `[r6,#0x24]`, `[r4,#12]`). If the background's name never reaches the field, the bind is issued
  with a stale name and the draw silently lands on the previous texture.
* `#105 glTexSubImage2D` and `#84 glPixelStorei` are both live in Zuma and appear in no draw log
  quoted so far. A background uploaded via `glTexImage2D` and then *patched* via
  `glTexSubImage2D` into a different name is worth checking.
* The other lead is the **`#159` pipeline index**. Zuma selects a pipeline at seventeen sites, and
  §12.11 records that the fifty pipeline programs live past the end of the image and cannot be
  read. If the background quad selects a pipeline the emulator collapses to the same shader as
  everything else, its texture fetch could be being dropped rather than its bind.

None of that is resolved here, and this section does not claim it is.

### 15.7 What the runtime traces say that the binaries do not

The task notes say *"Both games call `#45`, `#148`, `#165`, `#167`; Pac-Man also calls `#158`."*
For Zuma that matches the image exactly. **For Pac-Man it does not.**

Scanning every `B`/`BL` whose target lands in the thunk table (`0x64 + 4*ordinal`, §12.1) of
`Games_RO/AAAAA/Pacman_1_1_2563976.bin` — the binary §14.1 is about, identified by its
`0x1802c5f8` channel table — gives:

```
4(2) 12(3) 13(2) 35(2) 36(3) 37(6) 38(2) 40(6) 99(2) 125(1) 137(16) 157(1) 158(1) 159(2) 167(1)
```

There is **no `#45`, no `#148`, no `#165`**, and no indirect reference either: a search of the whole
image for the literals `0x180002b4`/`0x180005a0` (the `#148` thunk and its resolved-pointer slot)
and the equivalents for `#45` and `#165` finds nothing. `Games_RO/14004/mspacman_1_1_2805293.bin`
does not call them either. So one of three things is true, and this section cannot choose:

1. the traced title is a third Pac-Man build not in `Games_RO`;
2. the tracer attributes some calls to the wrong ordinal; or
3. the counts were aggregated across titles.

Worth resolving before `#148` is blamed for the power pellets, because Pac-Man's colours cannot be
coming from `#148` in the binary that is on disc. What that binary does instead is select
pipelines through a wrapper at `0x4414` that adjusts the attribute set per index:

```asm
4438:  bl 0x2e0                ; #159 glSetPipelineAPPLE(index)
4444:  cmp r0,#2  ; beq -> #40 glEnableVertexAttribArray(1)
444c:  cmp r0,#25 ; beq -> #40 enable(1) + #36 disable(2)
4454:  cmp r0,#30 ; beq -> #40 enable(1) + #36 disable(2)
445c:  cmp r0,#31 ;      -> #40 enable(1) + #36 disable(2)
44d4:  bl 0x258                ; #125 glUniformMatrix4fv(0, 1, 0, 0x1802c6a8)
```

**Attribute 2 is a per-vertex colour** in some pipelines (Lost supplies it as `#137(2, 4, GL_FIXED,
…)`, §12.1), and Pac-Man's wrapper explicitly *disables* it for indices 25, 30 and 31. So this
driver has two colour sources — per-vertex attribute 2, and the constant register that `#148`
writes — and which one is live is a property of the `#159` index. A renderer that models neither
will get white dots wrong in both games, for two different reasons.

One more measured detail from the same file: Pac-Man's attribute 1 is **not always a texcoord**.
At `0x3164` it is `#137(1, 4, GL_FIXED, 0, 0, ptr)` — four components — followed by pipeline 2 and
`glDrawArrays(1 = GL_LINES, 0, 2)`. At `0x354c` it is the usual `#137(1, 2, …)` with pipeline 25
and `glDrawArrays(7, 0, 4)`. Component counts must be taken from the call, never assumed.

### 15.8 Cross-check against `opengles-names.json`, and suggested entries

* **`#45 glGenTextures`** — already in the JSON, and now confirmed from the image's own string
  table (`0x2a957a`, quoted by the validator at `0x26e69c`). Alphabetical order agrees:
  `#44 glGenBuffers`, `#45 glGenTextures`, `#46 glGetAttachedShaders`. **Supports.**
* **`#148 glUniform4xvAPPLE`** — already in the JSON, and confirmed the same way (`0x2a97e4`,
  quoted at `0x2717f4`). It sits where §12.9 predicts, inside the `#139..#151` `*APPLE*` block, and
  its neighbours line up: `#147` the scalar `glUniform4xAPPLE` (§12.7), `#149
  glUniformMatrix4xvAPPLE` (string at `0x2a982f`), `#150/#151 glVertexAttrib4xAPPLE` /
  `glVertexAttrib4xvAPPLE` (`0x2a98e1`, `0x2a98f7`). **Supports.**
* **`#158`** — null in the JSON. §12.6 already had it right as "a 6-case dispatch on
  `r0 ∈ [0x3f000, 0x3f005]`". Confirmed and filled in.
* **`#165`, `#167`** — null in the JSON, and §12.9's "`#152..#178` are not GL entry points at all"
  is **too broad**. `#152..#164` is the render-server layer; `#165..#176` and `#178` are a
  `mat4`/fixed-point math library; `#177` is a raw block upload. Since the alphabetical run ends at
  `#138 glViewport`, ordering does not constrain any of them either way — but the *internal*
  ordering of the math block is orderly and self-confirming: float/fixed pairs at
  165/166, 169/170, 171/172, 173/174, 175/176. **No contradiction; a refinement.**

```
"45":  "glGenTextures(GLsizei n, GLuint* out) -- name allocator only, counter at ctx+0x270, first name is 1",
"148": "glUniform4xvAPPLE(GLint location, GLsizei count, const GLfixed* v4) -- 16.16; location 4 = per-draw modulate RGBA",
"158": "private enable/disable, pname 0x3F000..0x3F005, bool = (arg2 > 0) -> 0x3000, bad enum -> 0x300C",
"165": "mat4 loadIdentity (IEEE float, 16 words) -- pure, writes only *r0",
"166": "mat4 loadIdentity (16.16 fixed)",
"167": "mat4 ortho(m, left, right, bottom, top, zNear, zFar) -- floats, column-major, pure",
"168": "mat4 perspective(m, fovy, aspect, zNear, zFar)",
"169": "mat4 translatef",  "170": "mat4 translatex",
"171": "mat4 scalef",      "172": "mat4 scalex",
"173": "mat4 rotatef",     "174": "mat4 rotatex",
"175": "mat4 multMatrixf(dst, a, b)", "176": "mat4 multMatrixx(dst, a, b)",
"177": "raw block upload, opcode 10 (data, tagA, tagB, len)",
"178": "float[] -> 16.16 fixed[] conversion"
```

### 15.9 What an emulator has to implement

1. **`#45`**: `for i in 0..n-1: out[i] = ++counter`, counter per-context, **starting at 1**,
   separate from the buffer counter, never recycled. `n < 0` sets `GL_INVALID_VALUE`. Creating no
   object here is correct — the object appears on first `glBindTexture`.
2. **`#148`, and this is the one that matters**: store `4*count` raw 16.16 words into constant
   registers `1+location…` (locations 0–3) or `0x101+location-4…` (locations ≥ 4). At draw time,
   multiply the sampled texel by `register[0x101]` as `RGBA/65536`, clamped to `[0,1]`. Latch it
   per draw. Route `#120 glUniform4fv` and `#147` into the same store. `location == -1` is a
   no-op; `count < 0` is `GL_INVALID_VALUE`.
3. **`#158`**: accept `0x3F000..0x3F005`, keep the boolean `(arg2 > 0)` in a six-slot array, return
   `0x3000`; return `0x300C` otherwise. No rendering effect is known.
4. **`#165`**: one argument. Write the 16-float column-major identity to `r0`. Return nothing.
   Ignore `r1`/`r2`/`r3` — the traces show garbage there.
5. **`#167`**: seven arguments; the last three (`top`, `zNear`, `zFar`) are on the stack at
   `sp+0`, `sp+4`, `sp+8` at entry. Write the standard column-major `glOrtho` matrix to `r0`, with
   `m[12..14]` the translation and `m[15] = 1.0f`. Return nothing. Purely a memory write.
6. While in the neighbourhood, implement `#166`, `#168`–`#176` and `#178` too. They are twenty
   lines each, they are pure functions, and a game that calls `#171 scalef` and gets a no-op
   renders geometry at the wrong size with no other symptom.

### 15.10 What is not established

* **What the six `#158` selectors control.** Measured: the enum range, the boolean coercion, the
  opcode-11 packet, the return codes, and that `0x3F003`/`0x3F004` also set bits 9 and 10 of the
  ring flag word at `0x10871454`. The meaning is inside `rserver.bin`.
* **Which `#159` pipelines read constant register `0x101`.** The colour interpretation of
  `#148 location 4` is measured *from the caller side* — Zuma builds RGB565→16.16 RGBA immediately
  before every call, and ships literal `{1.0,1.0,1.0,1.0}` when it wants no tint. The consuming
  program object is at a fixed address past the end of the image (§12.11) and cannot be read. The
  reading is strong but it is caller-side inference, not a decode of the shader.
* **Whether the constant colour modulates or replaces.** Modulate is inferred from the
  white-means-neutral convention at `0x1802fc70` plus the 1×1 white texture at `0x120e0`; a
  replace-style combiner would make both of those pointless. Not decoded.
* **`#152`'s two output constants**, unchanged from §12.11.
* **Why Zuma's 322×222 background is never bound.** Ruled *out* here: none of the five can select a
  texture, and Zuma never calls `#0 glActiveTexture`. Not ruled out: a stale name in the struct
  field the seventeen `#4` sites read from, the `#105 glTexSubImage2D` path, and the `#159`
  pipeline index.
* **Which Pac-Man binary the traces came from.** The one in `Games_RO/AAAAA` calls neither `#45`,
  `#148` nor `#165`, directly or indirectly (15.7).
* Nothing in this section was executed.

## 16. What the five entries fixed, and the sampling bug beside them

§15 identified `#45`, `#148`, `#158`, `#165` and `#167`. Implementing them, plus one unrelated
rasteriser fix, corrected every outstanding rendering fault in the four working titles.

### 16.1 Texture sampling is BILINEAR, and that was Pac-Man's whole problem

Not cosmetic. Pac-Man draws its **entire maze as one image** — visible in its atlas, which
`--dump-tex=N` now writes out — scaled to the screen by a non-integer factor. Nearest-neighbour
sampling steps over whole texel rows and columns, so every one-pixel feature in that image was
skipped: the wall lines arrived dashed and **the entire pellet field vanished**, while the 12x12
sprites beside them sampled correctly. That is why the ghosts, text and panel art always looked
right and only the thin things were wrong.

Sampling is at texel centres (subtract half a texel before splitting into index and fraction), and
the colour-key cutoff is applied to the FILTERED alpha so a keyed edge fades instead of fringing.

### 16.2 `#148 glUniform4xvAPPLE` is the per-draw modulate colour

16.16 fixed, at constant register `0x0101` — location 4, the first slot past the MVP matrix.
`#120 glUniform4fv` is the same routine with a float conversion in front, so both feed one store.

**This is how the titles fill flat panels**: draw a 1x1 white texture with a colour in the
register. Ignoring it painted every such fill white — which is exactly what buried Zuma's menu and
level art under a white screen. `fragment = texel * register`, latched per draw.

### 16.3 `#167 ortho` makes the Y direction self-describing

Zuma calls `ortho(m, 0, 320, 240, 0, -50, 50)` — bottom **below** top, i.e. a Y-down projection.
Writing the matrix and reading element 5's sign sets the flip automatically, so Zuma no longer
needs `--flip-y` on the command line. (Bejeweled still does: its matrix is the identity, so there
is nothing in-band to read. See §13.6.)

`#165`/`#166` are `loadIdentity` in float and 16.16, pure maths with no driver state. `#158` is a
private enable/disable whose meaning lives in `rserver.bin`; returning `0x3000` and doing nothing
is correct. `#45 glGenTextures` hands out names from a counter that **starts at 1**, so 0 remains
distinguishable as "unbound".

### 16.4 Correction to §14.1

§14.1 blamed Pac-Man's rendering on its loader. That was wrong on two counts: the sound-loading
loop it describes was an artefact of `--load-on-open` filling a 44-byte header buffer (see the
whole-file rule), and the tan pellets were the sampling bug above, not a colour fault. Pac-Man
does not call `#45`, `#148` or `#165` at all.

## 17. The built-in pipeline table

**§12.11 was wrong about the one thing that mattered.** It recorded that the fifty `#159` pipeline
programs "live at fixed addresses in `0x108000a0..0x1081cf0c`, past the end of the `osos` image, so
their contents are not in any file we have." The addresses are indeed past the end of the image as
loaded at `0x10000000` — but **the bytes they must contain are in `osos.bin`, verbatim, at file
offset `0x67E344`**. All forty-five program objects, their descriptors and their microcode are
recoverable, and this section decodes them as far as they go.

Everything here is static reading of `osos.bin`, `Tetris_1_1_2563292.bin`
(`Games_RO/66666/Executables`, 153 324 bytes), `Zuma_1_1_2563298.bin`, `Pacman_1_1_2563976.bin` and
`Lost_1_1_2917525.bin`. Nothing was executed. All `osos` addresses are file offsets
(`VA = 0x10000000 + N`); game addresses are file offsets in the eApp (`VA = 0x18000000 + N`).

**Headline, up front, because it is the question that was asked:** **no built-in pipeline applies a
transform of any kind.** Every one of the five vertex programs does exactly one 4×4 matrix multiply
against attribute 0 and nothing else. There is no translate, no scale, no per-draw offset in a
constant register, and no positional meaning in attribute 1, 2 or in attribute 0's `z`/`w`. Tetris
renders at (0,0) because the transform it *does* supply — `#169 glTranslatef` from 49 sites, folded
into the MVP by `#175 glMultMatrixf` immediately before `#125` — is not being applied. That is
§18's Rank-1/Rank-5 finding, reached here from the opposite direction and agreeing with it.

### 17.1 Where the programs actually are

`#159`'s forty-three distinct literals (pool `0x26b04c..0x26b0f4`) span
`0x108000a0..0x1081cf0c`, plus two computed objects: index 3's second object is
`0x108006bc + 28 + 0x1b800 = 0x1081bed8` (`26ab24: add r5,r0,#0x1b800`) and index 5's is
`0x10800d18 + 0x3ac = 0x108010c4` (`26ab34: add r5,r4,#0x3ac`). Forty-five objects in all.

Sort them and the gaps are self-describing: the objects are laid out **contiguously**, so
`addr[i+1] - addr[i] - 28` is object `i`'s code length. Searching `osos.bin` for the single file
offset `F` at which `word32[F + (addr - 0x10800000) + 24] == gap - 28` holds for the first eight
objects returns exactly one candidate, and at that candidate **all forty-four testable objects
agree**, 44/44, with no near misses:

```
file offset = VA - 0x10800000 + 0x67E2A4          (equivalently: VA = file + 0x10181D5C)
first object 0x108000A0  ->  file 0x67E344
last  object 0x1081CF0C  ->  file 0x69B1B0,  ending at 0x69B8F2
blob extent: file 0x67E344 .. 0x69B8F2  =  120 238 bytes
```

Two independent confirmations that the mapping is right and not a coincidence:

* every one of the forty-five objects begins with `0x00008B31` or `0x00008B30` — **`GL_VERTEX_SHADER`
  and `GL_FRAGMENT_SHADER`** — and `#159`'s "object A" is always `8B31` and "object B" always
  `8B30`, matching `#160`'s blob layout (`objA`, then `objB` after `objA`'s code, §12.6);
* the pairing is structurally consistent in a way nothing random could be: the "varying count" word
  of the vertex program equals that of its fragment partner in **all forty-one** cases.

**What places the blob at `0x10800000` was not found.** There is no copy loop in `osos.bin` that
references either end of the range, no scatter-load region table, and no literal `0x1067E344`. The
image header words at file `0x300..0x310` (`40005ff8 40003ff8 047358c0 10800004 0067e264`) name
`0x10800004` and `0x0067e264` next to each other, which is suggestive but does not reproduce the
measured `0x44` delta, so it is not claimed as the answer. Practically it does not matter: an
emulator that wants the table can read the bytes straight out of `osos.bin` at the offsets above.

**The programs are *not* in `rserver.bin`.** A 32-byte sample of the first vertex program does not
occur in it, and the only `8B30`/`8B31` word in the whole 105 020 bytes is at `0x5214`, isolated.
`rserver.bin` is written in what looks like the *same* 16-bit instruction family (its payload at
`0x200` opens `782f 600c 0100 792f 3f5a 0200 c16f b92e …`, the same shape as the shader streams
below), which is consistent with it being the render server's own firmware rather than a shader
store. `#164` remains "hand the driver the render-server image"; it has nothing to do with `#159`.

### 17.2 The 28-byte program descriptor

`#159` uploads `objA[0..27]` plus `objA[24]` bytes of code from `objA + 28` (`26af00: mov r2,#28 /
str r2,[sp,#8]`, `26af0c: str r0,[sp,#12]` with `r0 = objA + 28`). Reading the forty-five headers
gives the layout unambiguously:

| word | offset | vertex (`8B31`) | fragment (`8B30`) |
|--:|--:|---|---|
| 0 | `+0` | `0x00008B31` | `0x00008B30` |
| 1 | `+4` | always `0` | flags; **bit `0x10000` = enable `GL_DEPTH_TEST`** |
| 2 | `+8` | always `4` — the four constant registers of the MVP | resource count, `0`/`1`/`2` (17.5) |
| 3 | `+12` | **number of vertex attributes consumed**, 1/2/3 | always `0` |
| 4 | `+16` | **number of varyings**, 1/2/3 | **number of varyings**, always equal to the vertex program's |
| 5 | `+20` | `16 × word4` — the varying block size in bytes | always `0` |
| 6 | `+24` | code length in bytes | code length in bytes |

Word 1 of the *fragment* program is the one `#159` reads back at the very end of every call,
whether or not the index changed:

```asm
26afbc:  ldr r0,[r4,#4]        ; r4 = &cache[index]; [r4+4] = fragment object
26afc0:  ldr r0,[r0,#4]        ; fragment word 1
26afc4:  tst r0,#0x10000
26afc8:  ldr r0,[pc,#304]      ; 0x26b100 = 0x0B71  GL_DEPTH_TEST
26afcc:  beq 0x26afd8
26afd0:  bl  0x26e240          ; #39 glEnable
26afd8:  bl  0x26d6e8          ; #35 glDisable
```

The observed flag values are `0x000004`, `0x020004`, `0x030004` and `0x060004`. Bit `0x20000` is
set exactly when the program has ≥ 2 varyings and bit `0x40000` exactly when it has 3, so those two
are a varying-usage mask. Bit `0x10000` is set in **one program only**, `0x1081c590` — so
**`#159(4)` turns depth testing on and every other index turns it off**. That refines §18.5's
caveat from "a property of the selected pipeline" to a single named index.

### 17.3 Five vertex programs, and the attribute count is the whole story

Only five distinct vertex programs exist behind the forty-one built-in indices:

| | address | file | attrs | varyings | code | used by indices |
|---|---|---|--:|--:|--:|---|
| **V0** | `0x108000a0` | `0x67E344` | 1 | 1 | 736 | 0, 1 |
| **V1** | `0x108006bc` | `0x67E960` | 2 | 2 | 772 | 2, 3, 4 |
| **V2** | `0x1080039c` | `0x67E640` | 2 | 2 | 772 | 6, 8, 10, 11, 13, 14, 15, 16, 17, 18, 20, 21, 22, 25, 27, 30, 31, 33, 36, 38, 39, 40, 49 |
| **V3** | `0x108009dc` | `0x67EC80` | 3 | 3 | 800 | 7, 9, 12, 19, 23, 24, 26, 28, 29, 32, 34, 35, 37 |
| **V4** | `0x10800d18` | `0x67EFBC` | 3 | 2 | 910 | 5 |

The attribute count is measured (descriptor word 3) and it is confirmed from three games
independently — every title's `glVertexAttribPointer`/`glEnableVertexAttribArray` set matches the
family of the index it selects, and Pac-Man's wrapper switches attributes *per index* in exactly the
pattern the table predicts (17.7).

`V1` and `V2` have **identical descriptors and identical first 410 bytes of code**, then diverge —
they are the same transform with a different second-attribute path. That is the shape of "attribute
1 is a colour" versus "attribute 1 is a texture coordinate", and 17.7 confirms it from the callers.

### 17.4 The table

`#159`'s jump table at `0x26aa18..0x26aadc` is 50 entries. Indices 0–40 and 49 select fixed pairs;
41–48 read the user slots at `0x1084bb44`/`0x1084bb84` (§12.6, unchanged); **49 branches to the same
handler as 13** (`26aadc: b 0x26ab94`).

| idx | vertex | fam | attrs | fragment | flags | w2 | code | prologue | depth |
|--:|---|---|--:|---|---|--:|--:|:-:|:-:|
| 0 | `108000a0` | V0 | 1 | `1081b5fc` | `000004` | 1 | 998 | yes | |
| 1 | `108000a0` | V0 | 1 | `1081b5fc` | `000004` | 1 | 998 | yes | |
| 2 | `108006bc` | V1 | 2 | `1081ba00` | `020004` | 0 | 1212 | | |
| 3 | `108006bc` | V1 | 2 | `1081bed8` | `020004` | 0 | 1692 | | |
| 4 | `108006bc` | V1 | 2 | `1081c590` | `030004` | 0 | 2400 | | **DEPTH** |
| 5 | `10800d18` | V4 | 3 | `108010c4` | `020004` | 2 | 2616 | yes | |
| 6 | `1080039c` | V2 | 2 | `10801b18` | `020004` | 2 | 1608 | yes | |
| 7 | `108009dc` | V3 | 3 | `1080217c` | `060004` | 1 | 2366 | | |
| 8 | `1080039c` | V2 | 2 | `10802ad8` | `020004` | 2 | 3096 | yes | |
| 9 | `108009dc` | V3 | 3 | `1080370c` | `060004` | 1 | 3854 | | |
| 10 | `1080039c` | V2 | 2 | `10804638` | `020004` | 2 | 1656 | yes | |
| 11 | `1080039c` | V2 | 2 | `10804ccc` | `020004` | 2 | 2800 | yes | |
| 12 | `108009dc` | V3 | 3 | `108057d8` | `060004` | 1 | 3582 | | |
| 13 | `1080039c` | V2 | 2 | `108065f4` | `020004` | 1 | 1858 | | |
| 14 | `1080039c` | V2 | 2 | `10806d54` | `020004` | 1 | 4178 | | |
| 15 | `1080039c` | V2 | 2 | `10807dc4` | `020004` | 2 | 3744 | yes | |
| 16 | `1080039c` | V2 | 2 | `10808c80` | `020004` | 2 | 4224 | yes | |
| 17 | `1080039c` | V2 | 2 | `10809d1c` | `020004` | 2 | 4032 | yes | |
| 18 | `1080039c` | V2 | 2 | `1080acf8` | `020004` | 1 | 1186 | | |
| 19 | `108009dc` | V3 | 3 | `1080b1b8` | `060004` | 1 | 2014 | | |
| 20 | `1080039c` | V2 | 2 | `1081cf0c` | `020004` | 2 | 1830 | | |
| 21 | `1080039c` | V2 | 2 | `1080b9b4` | `020004` | 1 | 2778 | | |
| 22 | `1080039c` | V2 | 2 | `1080c4ac` | `020004` | 2 | 2824 | yes | |
| 23 | `108009dc` | V3 | 3 | `1080cfd0` | `060004` | 1 | 3606 | | |
| 24 | `108009dc` | V3 | 3 | `1080de04` | `060004` | 2 | 4132 | yes | |
| 25 | `1080039c` | V2 | 2 | `1080ee44` | `020004` | 1 | 1730 | | |
| 26 | `108009dc` | V3 | 3 | `1080f524` | `060004` | 1 | 2558 | | |
| 27 | `1080039c` | V2 | 2 | `1080ff40` | `020004` | 1 | 3890 | | |
| 28 | `108009dc` | V3 | 3 | `10810e90` | `060004` | 1 | 4238 | | |
| 29 | `108009dc` | V3 | 3 | `10811f3c` | `060004` | 1 | 4718 | | |
| 30 | `1080039c` | V2 | 2 | `108131c8` | `020004` | 1 | 1146 | | |
| 31 | `1080039c` | V2 | 2 | `10813660` | `020004` | 1 | 1626 | | |
| 32 | `108009dc` | V3 | 3 | `10813cd8` | `060004` | 1 | 1974 | | |
| 33 | `1080039c` | V2 | 2 | `108144ac` | `020004` | 1 | 3186 | | |
| 34 | `108009dc` | V3 | 3 | `1081513c` | `060004` | 1 | 3534 | | |
| 35 | `108009dc` | V3 | 3 | `10815f28` | `060004` | 1 | 4014 | | |
| 36 | `1080039c` | V2 | 2 | `10816ef4` | `020004` | 2 | 5382 | yes | |
| 37 | `108009dc` | V3 | 3 | `10818418` | `060004` | 1 | 6164 | | |
| 38 | `1080039c` | V2 | 2 | `10819c48` | `020004` | 1 | 1272 | | |
| 39 | `1080039c` | V2 | 2 | `1081a15c` | `020004` | 1 | 1712 | | |
| 40 | `1080039c` | V2 | 2 | `1081a828` | `020004` | 1 | 3512 | | |
| 41–48 | *user slot 0–7* | — | — | *user slot* | — | — | — | | |
| 49 | `1080039c` | V2 | 2 | `108065f4` | `020004` | 1 | 1858 | | *= 13* |

The four families collapse the fifty indices to four *interfaces*. Everything else — the forty
distinct fragment programs — varies only in what it does with the same inputs.

### 17.5 What the fragment programs are made of

The fragment streams are 16-bit little-endian instructions and open with a run of `28NN 01MM`
declaration pairs. Grouping the forty programs by that opening run:

```
untextured, vertex-colour  (idx 2, 3):   f023  2802 0130  2803 0128  2804 012c            2800 010c
   the same plus one more (idx 4):       f023  2802 0130  2803 0128  2804 012c  2805 0138  2800 010c
textured                  (most):        f023  2802 0130  2803 0128  2809 012c  2807 0120  2800 010c
position-only             (idx 0, 1):    f023  <prologue>  2022 20e0 03b4 780f 0087  2804 012c  2800 010c
```

`2802`/`2803` look like varying declarations: the V0 program — the only one with a single varying —
is the only one with no `2802`.

So `2807` is present in **every** V2/V3-family program and in **none** of the V0/V1-family ones —
it is the texture-unit declaration, and it never appears twice. **Every built-in pipeline samples
either zero or exactly one texture.** That kills, statically, the §15.6 lead that a pipeline index
might be dropping a texture fetch: there is no multi-texture built-in for a game to select.

Twelve of the forty fragment programs carry an extra fixed **18-halfword prologue** ahead of the
declarations:

```
2803 011c  2002 cd00 001f  0000 40e7  2202 ed00 081f  2022 ede0 07ff  fc0e 1807  2222 ede0 0500
```

and where a prologue-carrying and a prologue-free program share a family, the relationship is
literal concatenation. Index 6's fragment program is byte-for-byte index 18's with those 36 bytes
spliced in after the leading `f023`:

```
idx6  [38:76] = 022830010328280109282c010728200100280c01022200c31710022200e31718022200c41f40
idx18 [ 2:40] = 022830010328280109282c010728200100280c01022200c31710022200e31718022200c41f40
```

The prologue's immediates — `0x001f`, `0x07ff`, `0x0500`, `0x40e7` — read like RGB565 field masks
and shifts, so the most natural reading is **"unpack the destination framebuffer pixel"**, i.e. this
is the blend-against-the-framebuffer variant of the same shader. That is **inference from three
constants, not a decode**, and it is flagged as such.

Descriptor word 2 (`w2`) counts something that increments once for the prologue and once for the
texture declaration: `0` for the untextured vertex-colour pipelines 2/3/4, `1` for a program with
either a texture or the prologue, `2` for one with both. It fits "number of memory streams the
fragment program reads" for 39 of the 40 programs. **Index 20 is the exception** — `w2 = 2` with
one `2807` and no prologue — so the reading is not proven and word 2 should be treated as
undecoded.

`w2` is *not* a count of constant registers. The register `#148 location 4` writes (tag `0x0101`,
§15.3) is read by pipelines with `w2 = 1` and `w2 = 2` alike — Zuma's tinting works through
pipelines 15 and 16, both `w2 = 2`, and §16.2 measured that `fragment = texel × register[0x101]`
is the correct rule for them while setting only location 4. Nothing here changes that rule.

### 17.6 The vertex programs do exactly one matrix multiply, and nothing else

The vertex streams contain an obvious repeating multiply–accumulate block whose tail is the
8-byte signature `01 67 ed ca 0d 69 1d cf`. V0's code bytes `0x38`–`0x18a` are sixteen of them,
back to back (halfword offsets into the code, not the descriptor):

```
0038  b85e b8cd  2e81 01ae 6701 caed 690d cf1d  b9bd        <- block 0,  source b8
004a  e15e b8cd  2e81 01ae 6701 caed 690d cf1d  f1bd        <- block 1,  source b8
005c  e25e b8cd  2e81 01ae 6701 caed 690d cf1d  f2bd
006e  e35d b8ce  2e81 01cd 6701 caed 690d cf1d  f3bd
0080  e45d e1ce  2e81 01cd 6701 caed 690d cf1d  b8b1 ccd1 b9b1
...
0172  ef5e e3cd  2e81 01ae 6701 caed 690d cf1d  e3b1 ccd1 f3b1   <- block 15, source e3
```

Read the second halfword of each block down the column and it is `b8 b8 b8 b8 | e1 e1 e1 e1 | e2 e2
e2 e2 | e3 e3 e3 e3` — **four source registers, four destinations each**. That is a `mat4 × vec4`
and it is the only structure of its kind in the program. Descriptor word 2 is `4` for every vertex
program, which is the same fact stated in the header: **four constant registers, i.e. tags
`0x0001..0x0004`, i.e. exactly the `mat4` that `#125 glUniformMatrix4fv(0, …)` uploads.**

Counting the signature across all five:

| program | signature count | reading |
|---|--:|---|
| V0, V1, V2, V3 | **19** | 16 (`mat4 × attribute 0`) + 3 (a common tail) |
| V4 | **25** | 16 + 3 + **6 extra** |

The three-block tail is identical in all five and sits after the immediates `2f6e 0000 4000`
(`0x40000000` = 2.0f), `2f6d 0000 4fff` and `2f6d 0000 b001` — a reciprocal and a pair of clamp
bounds, i.e. the perspective divide and viewport scale. It consumes nothing the caller supplies.

The consequences are the answer to the load-bearing question:

* **No vertex program reads a fifth constant register.** Word 2 is `4` in all five. A translate or
  scale sourced from `#148 location 4` (tag `0x0101`), or from any other constant, is impossible —
  there is no such read in the descriptor and no second matrix multiply in the code.
* **No vertex program transforms anything but attribute 0.** V1, V2 and V3 have exactly the same
  nineteen blocks as V0, which has only one attribute. The second and third attributes are carried
  to the fragment stage with **no arithmetic applied**.
* **Attribute 0's `z` and `w` are the third and fourth components of the `mat4 × vec4`** and nothing
  else. Tetris's measured `(0.0, 1.0)` is the ordinary `z = 0, w = 1`.
* **Index 5 (V4) is the one that does extra work** — six additional multiply–accumulates, i.e. a
  2×3 or 3×3 affine, on top of the standard transform, which is why it is the only family with more
  attributes (3) than varyings (2): two of its inputs are combined into one output. What it
  computes was **not decoded**. No title read here selects index 5.

### 17.7 Attribute semantics, confirmed from three games in three different ways

The families predict the attribute set; the games supply it. All three agree.

**Tetris** has one draw function, `0x1da40`, and it chooses its attribute set from two flag bytes on
the batch — `[r0+0x4a]` "has texture coordinates" (`0x71f4`) and `[r0+0x49]` "has vertex colour"
(`0x73fc`):

```asm
1da80:  mov r2,r6 ; mov r3,#0 ; mov r1,#4 ; mov r0,#0
1da90:  bl  0x288             ; #137 glVertexAttribPointer(0, 4, 0x140C, 0, 0, batch+0xa8)
1da98:  bl  0x104             ; #40  glEnableVertexAttribArray(0)
1daa0:  bl  0x71f4            ; has texcoord?
1daf0:  bl  0x288             ; #137 (1, 2, 0x140C, 0, 0, batch+0xe8)     -- only if it does
1daf8:  bl  0x73fc            ; has colour?
1db4c:  mov r0,#2 ; bl 0x288  ; #137 (2, 4, 0x140C, 0, 0, batch+0x108)    -- colour, WITH texcoord
1db5c:  mov r0,#1 ; bl 0x288  ; #137 (1, 4, 0x140C, 0, 0, batch+0x108)    -- colour, NO texcoord
```

That is precisely the three interfaces: position only → V0; position + 4-component colour in
attribute **1** → V1; position + 2-component texcoord → V2; position + texcoord + 4-component colour
in attribute **2** → V3. Tetris's measured index set matches its own branches:

| index | family | what Tetris must be supplying |
|--:|---|---|
| 3 | V1 | position + **colour in attribute 1**, untextured |
| 8, 14, 27 | V2 | position + texcoord, textured |
| 19, 35 | V3 | position + texcoord + colour in attribute 2, textured |

**Pac-Man** is the strongest confirmation, because §15.7 already measured its wrapper at `0x4414`
adjusting attributes *per index* and could not explain why:

```asm
4444:  cmp r0,#2  ; beq -> #40 glEnableVertexAttribArray(1)
444c:  cmp r0,#25 ; beq -> #40 enable(1) + #36 disable(2)
4454:  cmp r0,#30 ; beq -> #40 enable(1) + #36 disable(2)
445c:  cmp r0,#31 ;      -> #40 enable(1) + #36 disable(2)
```

Index 2 is **V1** (attribute 1 is the colour) and Pac-Man feeds it `#137(1, 4, GL_FIXED, …)` at
`0x3164` followed by `glDrawArrays(GL_LINES, 0, 2)` — a coloured line with no texture, and
`0x1081ba00`'s descriptor says exactly that: no `2807` declaration, `w2 = 0`. Indices 25, 30 and 31
are all **V2**, which has only two attributes, so attribute 2 *must* be disabled — and the wrapper
disables it for those three and no others. The wrapper is a family selector.

**Zuma** uses 15 and 16, both **V2**, and its draw block (§15.3, `0x230b4`–`0x230e4`) supplies
`#137(0, 4, GL_FIXED)` and `#137(1, 2, GL_FIXED)` and nothing else, with the colour in
`#148 location 4`. Exactly V2's interface.

**Lost**'s literal indices 1, 2, 3, 14, 18, 39 are V0, V1, V1, V2, V2, V2 — all ≤ 2 attributes —
while its sprite flush at `0x50f4` supplies attributes 0, 1 **and 2** (§12.1). So the indices Lost
uses for sprites are the data-driven ones, and they must be V3 indices. An emulator that wants to
sanity-check a trace can use that: **a draw that enables attribute 2 and selects a non-V3 index is a
bug in the trace, not in the game.**

### 17.8 So where is Tetris's transform?

Not in the pipeline. Tetris builds it itself, with the matrix library of §15.4, and hands the
finished MVP to `#125` on **every** draw:

```asm
; Tetris 0x1db6c -- the last five instructions before every glDrawArrays
1db6c:  ldr r2,[pc,#116]   ; 0x180254c8   modelview
1db70:  ldr r1,[pc,#116]   ; 0x18025488   projection
1db74:  ldr r0,[pc,#116]   ; 0x18025508   scratch
1db78:  bl  0x320          ; #175 glMultMatrixf(scratch, projection, modelview)
1db7c:  ldr r3,[pc,#108]   ; 0x18025508
1db80:  mov r2,#0 ; mov r1,#1 ; mov r0,#0
1db8c:  bl  0x258          ; #125 glUniformMatrix4fv(0, 1, 0, scratch)
1db90:  mov r2,#4 ; mov r1,#0 ; mov r0,#7
1db9c:  bl  0xf8           ; #37  glDrawArrays(7, 0, 4)
```

The three matrices are consecutive 64-byte slots: projection `0x18025488`, modelview `0x180254c8`,
scratch `0x18025508`, with the current-matrix pointer at `0x18025484`. `0x3aa0` is the matrix-mode
setter and it is two instructions:

```asm
3aa0:  ldr r1,[pc,#16]     ; 0x18025478
3aa4:  cmp r0,#1
3aa8:  ldreq r0,[pc,#12]   ; 0x18025488   mode 1 -> projection
3aac:  addne r0,r1,#80     ; 0x180254c8   otherwise -> modelview
3ab0:  str r0,[r1,#12]     ; -> 0x18025484, the current matrix
```

Setup at `0x5508` does `#165 loadIdentity` on the projection, then

```asm
5544:  mov r3,#0x3f800000            ; 1.0f
5548:  orr r2,r3,r3,lsl #8           ; 0xbf800000 = -1.0f
554c:  add r1,r3,#0x3f00000          ; 0x43700000 = 240.0f
5550:  stm sp,{r1,r2,r3}             ; top = 240.0, zNear = -1.0, zFar = 1.0
5554:  add r2,r1,#0x300000           ; 0x43a00000 = 320.0f = right
5558:  ldr r0,[pc,#48]               ; 0x18025488
555c:  mov r1,#0 ; mov r3,#0         ; left = 0, bottom = 0
5564:  bl  0x300                     ; #167 ortho(m, 0, 320, 0, 240, -1, 1)
```

— **Y-up**, `m[5] = 2/240 > 0`, the Pac-Man convention rather than Zuma's (§16.3), and it confirms
320×240 for a fourth title.

Then all forty-nine `#169` sites translate the *modelview*, in the classic place-draw-unplace idiom:

```asm
; Tetris 0xc030 -- one object, placed and restored
c03c:  ldr r0,[r8]                   ; r8 = 0x18025484, the current matrix
c040:  mov r3,#0 ; mov r1,r7 ; mov r2,r6
c048:  bl  0x308                     ; #169 translatef(m, x, y, 0)
c058:  bl  0x161b0                   ; the object itself
c060:  eor r2,r6,#0x80000000         ; -y
c064:  eor r1,r7,#0x80000000         ; -x
c06c:  bl  0x308                     ; #169 translatef(m, -x, -y, 0)
```

`#175` at `0x273db8` is `dst = a × b` column-major (`ldr r0,[r4,#0]` × `ldr r1,[r5,#0]` +
`[r4,#16]` × `[r5,#4]` + `[r4,#32]` × `[r5,#8]` + `[r4,#48]` × `[r5,#12]` → `str r0,[r6]`), so
Tetris's scratch is `projection · modelview`, correctly ordered.

**Therefore**: with `#175` implemented and `#169` a no-op, the uploaded MVP is
`ortho(0,320,0,240,-1,1) · I`, every batch's model-space vertices land at their own local origin,
sizes are exact and positions are all zero. That is the reported symptom precisely, and it is the
same conclusion §18 reached from the coverage audit. Nothing in `#159`, `#148`, `#137` or the
pipeline table needs to change to fix it.

### 17.9 What an emulator must do, per pipeline index

1. **Keep `#159`'s index and map it to a family.** Only four interfaces exist for indices 0–40:

   | family | indices | attribute 0 | attribute 1 | attribute 2 | fragment |
   |---|---|---|---|---|---|
   | **V0** | 0, 1 | position (`mat4 ×`) | — | — | no texture; colour from constant `0x0101` |
   | **V1** | 2, 3, 4 | position | **colour**, 4 × 16.16 | — | no texture; colour = interpolated attribute 1 |
   | **V2** | 6, 8, 10, 11, 13–18, 20–22, 25, 27, 30, 31, 33, 36, 38–40, 49 | position | **texcoord** | — | one texture × constant `0x0101` |
   | **V3** | 7, 9, 12, 19, 23, 24, 26, 28, 29, 32, 34, 35, 37 | position | **texcoord** | **colour**, 4 × 16.16 | one texture × attribute 2 × constant `0x0101` |
   | **V4** | 5 | position | ? | ? | one texture; extra per-vertex affine, undecoded |

   Component counts still come from the `#137` call (§15.7); the *meaning* comes from the family.
2. **Never invent a transform.** Position is `MVP × attribute0`, full stop, with `MVP` the four
   constant registers `0x0001..0x0004` written by `#125`/`#149`. No index adds a translate, scale,
   offset or texture matrix (index 5 aside, which no title uses).
3. **`#159` toggles `GL_DEPTH_TEST` on every call**, from bit `0x10000` of the fragment program's
   flags word: **on for index 4, off for every other built-in index**. This happens whether or not
   the index changed, and it overrides whatever the game last said with `#35`/`#39`.
4. **Ignore per-vertex colour for V0/V1/V2 indices and honour it for V3.** A renderer that always
   modulates by attribute 2 will tint V2 draws with stale array contents; one that never does will
   flatten every V3 draw.
5. **Ignore the constant colour for V1 indices (2, 3, 4).** Those three fragment programs have
   `w2 = 0` and no texture: their output is the interpolated attribute-1 colour alone. Modulating
   them by register `0x0101` — which the game may have left set from an earlier draw — will tint
   Pac-Man's vector lines wrong.
6. **One texture unit, always.** No built-in program declares two samplers. `#0 glActiveTexture`
   never needs more than unit 0 for any built-in index.
7. **Indices > 49 are undefined behaviour in the driver**, not a no-op: the default arm
   (`26aa14: b 0x26ae9c`) leaves `r4 = r5 = 0` but the tail still indexes the 64-slot cache with the
   raw index (`ldr r1,[r8,r7,lsl #3]`) and then dereferences `[r4+4]` at `0x26afbc`. An emulator
   should clamp or ignore; the hardware would read out of bounds.
8. **`#160`'s user slots are unchanged** (§12.6) — the blob it takes is two of the 28-byte
   descriptors above, back to back with their code, which is why it demands `size ≥ 56`.

### 17.10 What is not established

* **The instruction set.** The programs are 16-bit little-endian streams with a recognisable
  declaration prologue (`28NN 01MM` in fragment programs, `2cNN 01MM` in vertex programs), a
  recognisable multiply–accumulate block, and 32-bit immediates introduced by `2fNN`/`2cNN`
  (`2f6e 0000 4000` is `2.0f`). That was enough to count matrix multiplies, attribute paths and
  texture declarations, and no more. **Nothing in this section decodes an actual opcode.** Every
  claim above rests either on the 28-byte descriptor (which is read directly) or on counting
  repeated byte patterns.
* **Descriptor word 2 of a fragment program.** `0`/`1`/`2`, fitting "memory streams read" for 39 of
  40 programs, with index 20 the exception. Not decoded.
* **What the 18-halfword prologue computes.** Present in indices 0, 1, 5, 6, 8, 10, 11, 15, 16, 17,
  22, 24, 36. Its immediates look like RGB565 masks, which suggests a framebuffer read for blending,
  but that is a guess from three constants.
* **Index 5 / family V4.** Six extra multiply–accumulates and one fewer varying than it has
  attributes. Not decoded, and no title read here selects it.
* **What places the blob at `0x10800000`.** The bytes are in `osos.bin` at `0x67E344` and the
  mapping is confirmed 44/44 on object lengths and 41/41 on varying counts, but the copy that puts
  them at the address `#159` uses was not found in the image.
* **Which of the forty fragment programs is which combiner.** The families give the *inputs*; the
  differences between, say, index 14 (4178 bytes) and index 30 (1146 bytes) are forty different
  fixed-function combiner setups, and telling them apart needs the ISA.
* **The exact per-index blend mode.** `glEnable(GL_BLEND)` is never called by any title (§18) and
  `#159` only toggles `GL_DEPTH_TEST`, so blending — if it exists — is baked into the fragment
  program. The prologue is the best candidate and it is unconfirmed.
* Nothing in this section was executed.

## 18. Coverage audit

A full sweep of the framework surface against what the emulator implements. Two questions: which
ordinals can the titles reach that we answer with a silent `0`, and which of those can produce
a **visible** fault. All `osos` addresses are file offsets (`VA = 0x10000000 + N`); game addresses
are file offsets in the eApp (`VA = 0x18000000 + N`). Everything here is static reading of
`osos.bin` and the eApps except where a runtime trace is quoted.

§18.1–18.7 are the original hand audit: OpenGLES only, six titles. **§18.0 supersedes its
numbers** and covers all eight frameworks across all eighteen titles. The OpenGLES material below
is still the reference for what each of those ordinals *does* — only the counts went stale.

### 18.0 The whole surface, all eighteen titles

The hand audit went stale within a week: seven of its twelve missing OpenGLES ordinals were
implemented, and two more (`#160`, `#168`) turned up in TWA, a title it never scanned. So the walk
is now a tool — `tools/eapp-loader/src/bin/covscan.rs` — and the number is reproducible:

```
covscan "20 iPod games/Games_RO" --impl=tools/eapp-loader/src/bin/play.rs --per-title
covscan "20 iPod games/Games_RO" --verify     # reproduces §18.1's Minigolf/Zuma/Pac-Man rows
```

`--verify` is the control. It reproduces the three §18.1 rows that were hand-counted without
ambiguity — same ordinals, same call-site counts, all twenty/twenty-two/fifteen of them. Those
were counted by a different person by a different route, so agreement is evidence the walk is
right rather than evidence it agrees with itself.

The audit's starting position, and where it ended (§18.0.2 is what closed it):

| framework | published | **called** | implemented *(was)* | **missing** *(was)* |
|---|--:|--:|--:|--:|
| OpenGLES | 179 | 38 | 36 *(29)* | **0** *(7)* |
| Audio | 61 | 39 | 39 *(8)* | **0** *(31)* |
| Metadata | 152 | 54 | 54 *(2)* | **0** *(31)* |
| AsyncFileIO | 17 | 11 | 11 *(8)* | **0** *(3)* |
| miscTBD | 15 | 13 | 12 *(6)* | **0** *(6)* |
| Filesytem | 4 | 3 | 3 *(2)* | **0** *(1)* |
| InputEvents | 2 | 2 | 2 *(1)* | **0** *(1)* |
| Settings | 3 | 1 | 1 *(0)* | **0** *(1)* |
| **total** | **433** | **161** | **158** *(56)* | **0** *(81)* |

The three not implemented are the soft entries §18.0.2 ends on: no binary contains a call site
for them.

The reframing that matters: **433 are published, but only 161 are ever called by any title.** The
thunk table is a fixed OS-wide surface, most of which no game touches. Coverage is 56/161, not
56/433.

**Soft** means the ordinal is reached only through an *orphan veneer* — a linker-interposed
`b <thunk>` that nothing branches to. §18.1 wrote those as `1v` and counted them. They are
separated because a stray `b` into the thunk array from misparsed data is indistinguishable from a
real one, and on a 700 KB image that happens. There were 24, 21 of them Metadata; 21 are now
covered as a side effect of §18.0.2 and three remain, listed at the end of it.

`OpenGLES #120` and `#166` are implemented and called by nothing.

### 18.0.1 The 81, ranked by how many titles cannot avoid them

```
18/18  Audio #1 #8 #9 #10 #11 #12 #13 #14 #15 #18 #51 · InputEvents #1 · Settings #0
       miscTBD #5 #6 #7
17/18  miscTBD #10
16/18  Audio #45 #56
14/18  Audio #5
13/18  Audio #17 #23
10/18  Audio #46
 9/18  Audio #47 · OpenGLES #35
 8/18  Audio #55 · miscTBD #11
 7/18  OpenGLES #101 · miscTBD #3
 6/18  Audio #3 #42
 5/18  Audio #39
 4/18  Audio #41 #53 · OpenGLES #0 #84
 3/18  Audio #4 #50 · Filesytem #1
 2/18  Audio #37 #44 #49 #60 · Metadata #0 #1 #2 #3 #66 #67 #68
 1/18  AsyncFileIO #7 #9 #10 · Audio #20 · OpenGLES #53 #160 #168
       Metadata #4 #5 #11 #13 #17 #29 #40 #41 #42 #45 #53 #54 #55 #58 #59 #60 #63 #65
                #69 #74 #93 #108 #114 #118
```

It is a small shared core and a long per-title tail, not a flat list. Sixteen of the nineteen
universal entries are one contiguous block — `Audio #8`–`#15` plus `#1`, `#18`, `#51` — which
reads like a single init/mixer-config sequence every title runs at startup.

The 31 Metadata entries belong almost entirely to **molly** (23) and **TWA** (15), the two titles
that browse the iPod's real music library. Every other title calls two Metadata entries or none.

Per title the median is 25–28 missing; TWA (57) and molly (48) are the outliers, mspacman (23) the
leanest.

**"Missing" is not the same as "broken."** An unimplemented ordinal returns 0 silently, and for
many of these that turned out to be the right answer — Bejeweled, Zuma, Pac-Man and Tetris all
*played* while missing 25+ entries each. §18.0.2 records which ones zero was already correct for,
which is most of them; the count that changed behaviour is small and named there.

**What the scan cannot see.** It walks direct branches, veneers and literal pools. Anything
dispatched through a computed pointer is invisible to it. §18.1 established that no title
references an OpenGLES thunk indirectly; that has not been checked for Audio or Metadata.

### 18.0.2 Closing the 81 — what each group turned out to be

All 81 are implemented, and so are 21 of the 24 soft entries: **158 of the 161 called ordinals**.
The three left over have no call site in any binary (below). What follows is what the reading
found, because roughly half of these are cases where *zero was already the right answer* and
saying so is worth as much as the ones that were wrong.

The single most useful tool for this was `dis --base=` — the static reader could only address the
firmware, so every question about **game** code ("what enum does this call site pass?") used to
need a boot. An eApp links at 0x18000000; one relocatable window and the same file answers
statically. Most of the findings below were read that way in seconds.

#### The one that was actually wrong

**`miscTBD #10` returns 1000.** `0x0026a2c4` is `mov r0,#0x3e8 ; bx lr` — a constant, and we were
answering 0 to seventeen of the eighteen titles. Its neighbour `#11` (`0x0026a2bc`) really is
`mov r0,#0 ; bx lr`, so the pair reads like a max/min. Anything dividing by `#10` divided by zero.

#### `miscTBD #5`/`#6`/`#7` — a level trio, and a hypothesis refuted

All three reach one singleton through the lazy getter at `0x001c2aa4` (`0x10800090`), whose
constructor at `0x001c2c48` is a bare `bx lr`. A `--wordref` sweep finds **exactly one** reference
to the object in the image, so these three functions are all that touch it.

* `#5(level)` clamps to `0..=100` (`cmp #0x64 / movgt`, `cmp #0 / movlt`) and stores at `+0x00`.
* `#6()` returns `+0x00`. **Four instructions, no arguments, no out-parameters.**
* `#7(enable)` stores a byte at `+0x04`; nothing exposes it, so it is observably a no-op.

`#6` having no out-parameters **refutes the `MemoryReport` hypothesis** — it was under test as a
two-out-param "how much memory is there", which fit the Sudoku/SimsBowling/SimsPool pool-exhaustion
symptom at 5.24 MB. That symptom is real and remains unexplained; whatever answers it is elsewhere.

What device the level drives is *not* established: the value goes through a scaling curve at
`0x00118fe8` and is applied by a vtable call `[[obj+0]+0x18]` on a driver singleton this reading
did not identify. Nothing needs it — no title reads back anything but the level.

#### `miscTBD #3` is `printf`, and no shipped title calls it

`0x00266d78` spills `{r0-r3}`, takes the spilled `r0` as the format and `&r1` as the va_list, and
calls `0x00286860` — a formatter that scans for `%`, `\`, `\n` and NUL. The register spill is the
identification: no fixed-arity function needs `{r0-r3}` on entry.

Seven binaries reference it. **Across full runs of all eighteen titles it produced zero output** —
the import survives in the release builds, the calls do not. Worth knowing before anyone else goes
looking for a game's own debug log: there isn't one.

#### Audio splits into two frameworks wearing one name

**The game's sound engine** (`0x0026axxx`) is a handle table. `0x0029cbc4(tracker, handle)` is
`handle >= 0 && handle < tracker[+4] ? tracker[+0][handle] : 0`, so a sound handle is an index and
everything else is one field of the descriptor it returns:

| ordinal | field | | ordinal | field |
|---|---|---|---|---|
| `#8` | `+0x08` word | | `#14` | `+0x24` word |
| `#9` | `+0x0c` byte | | `#15` | `+0x20` word |
| `#10` | `+0x10` word | | `#17` | `+0x3d` byte |
| `#11` | `+0x14` word | | `#18` | `+0x3e` byte |
| `#12` | `+0x18` word | | `#20` | `+0x28` word |
| `#13` | `+0x1c` word | | `#23` | `+0x04` word (**read**) |

None of them touches the mixer at call time, so what the fields *mean* is not established and does
not need to be — the emulator keeps them so a setter and a reader agree.

`+0x3d` is the transport state, and **state 1 = PLAYING is measured**: the play path behind
`Audio #2` (`0x001b9168`) ends with `strb #1,[desc+0x3d]` looped down the `+0x40` sibling chain,
and `#3`/`#4`/`#5` are the identical loop with the constant fixed at 2, 1 and 3. That makes
`Audio #39` (`0x0026a4dc`: `ldrb [+0x3d] ; cmp #1`) an `isPlaying`, answerable for the first time.
Which of 2 and 3 is *pause* and which is *stop* is **not** settled; `#5` is treated as stop because
fourteen titles call it against six for `#3`, and because the cost of being wrong that way is a
paused sound cut short rather than a stopped sound looping forever. `Audio #1` is the tracker's
release (`0x0029caac`, virtual destructor), so it stops the voice too.

**The iPod's own music player** (`0x00268xxx`) is the other half, and nothing here can serve it.
Commands allocate a 12-byte message (`0x001301a0`), fill in an id (`0x001300f4`), find the player
task (`0x0012e520`) and post it (`0x0012d930`) — `#41`→`0x6600000e`, `#42`→`0x66000010`,
`#44`→`0x66000012`, `#45`→`0x66000015`, `#46`→`0x66000013`, `#50`→`0x66000016`,
`#53`→`0x66000019` (volume, argument scaled to 255). Queries read `[[0x1081da18]+0x1c]`.
There is no player task, and with nothing playing the device answers 0 to every query anyway —
`#51` computes `min(pos,len)*255/len`, a 0..255 progress ratio, which is 0 before playback starts.
`#37` reads the descriptor's attached voice at `+0x34` and Apple's own code has `moveq r0,#0` for
the no-voice case.

#### `Settings #0` is `("Language" | "TimeFormat", void *out, int *size)`

The dispatcher at `0x002686a8` matches `name` against a three-entry table at `0x10800050` filled at
runtime, so the names are not in the image — but the callers name them, and there are only two.
Every title asks for `Language`; the ten that draw a clock also ask for `TimeFormat`.

Ms. PAC-MAN gives both, and they are read differently:

```asm
180029b4  str r0,[sp,#4]     ; out = 0   <- pre-zeroed
180029cc  bl <Settings #0>   ; ("Language", sp+4, sp), size = 4
180029d0  ldr r1,[sp,#4] ; cmp r1,#0x18 ; addls pc,pc,r1,lsl #2   ; a 25-way jump table

18002c1c  str r0,[sp,#0]     ; size = 4;  out NOT initialised
18002c2c  bl <Settings #0>   ; ("TimeFormat", sp+4, sp)
18002c3c  bl 0x18001398      ; strcmp(out, "12")   <- the literal is at 0x18002c5c
18002c40  cmp r0,#0 ; movne r4,#1                  ; is24 = out != "12"
```

So `Language` is a **word** used as a 0..24 index and `TimeFormat` is a **string**. The
`TimeFormat` path was a live bug in the unimplemented version: the game never zeroes that buffer,
so `strcmp` ran against uninitialised stack and the 12/24-hour choice was whatever was left there.
`Language` is the opposite — every caller pre-zeroes it, so answering nothing already meant
language 0. This is also why Vortex ships `BonusTypes_de/es/fi/fr/it/nl/no/sv.ipd` and Ms. PAC-MAN
ships `tex_en_ui`, `tex_eu_text0`, `tex_jp_font`: they pick their artwork from this one call.

#### OpenGLES: four entries, and three of them were already right

* **`#35 glDisable`** — scanning every call site in all eighteen binaries for the enum in `r0`
  turns up exactly two values: `GL_CULL_FACE` (`0x0B44`, seven titles) and `GL_DEPTH_TEST`
  (`0x0B71`, Pac-Man). This rasteriser paints quads in submission order and has neither, so
  switching them off is already its behaviour. **Nothing disables `GL_BLEND`** — the one that
  would have mattered — and `#39 glEnable` is not called by any title at all.
* **`#53 glGetError`** — `0x0026ea10` reads and zeroes `ctx+0x88`. Nothing here ever sets an
  error, so `GL_NO_ERROR` is the answer rather than a placeholder. Minigolf calls it 24 times.
* **`#101 glTexParameterf`** — validates min/mag filter and wrap mode through the pure validator
  at `0x00107a58` and then discards them; only `GL_TEXTURE_PRIORITY` reaches the hardware (§18.3).
* **`#0 glActiveTexture`** and **`#84 glPixelStorei`** are real state. `#84`'s parameter must be
  1/2/4/8 or the driver panics, `GL_UNPACK_ALIGNMENT` (`0x0CF5`) lands at `ctx+0x268` and
  `GL_PACK_ALIGNMENT` (`0x0D05`) at `ctx+0x264`. Every title's textures are 320×240 or
  power-of-two at 2 or 4 bytes a texel, so the alignment cannot change a byte of any upload seen
  so far. `#0` is recorded but **only texture unit 0 is modelled**; Vortex passes `GL_TEXTURE1` at
  one of its two call sites, and the emulator now says so instead of silently sampling unit 0.

#### Metadata is an empty music library, which is a real device state

Only molly (23 ordinals) and TWA (15) reach this framework, and both browse the iPod's own
library. There is no iTunesDB behind this emulator, so zero artists, zero albums and
out-of-range for every index is what an iPod with no music on it reports. Three values are not
zero, and each would be a bug if it were:

* `#0 MusicLibraryCreate` returns a **handle** (`-1` on failure); 0 is a plausible Tracker index,
  so a distinct non-zero handle keeps "created" apart from "failed".
* `#125` is the now-playing **current index** and `-1` means none — 0 would claim the first track
  of an empty queue is playing.
* `#53`/`#54`/`#55`/`#58` return **`-50`** for an out-of-range index. §11.7 flags this
  specifically as a value a port must copy rather than invent.

`#43` is the subtle zero: it returns the playlist count **minus one**, excluding the master
library playlist, and an empty library still has that one — 1 − 1 = 0. The string getters write a
terminator rather than returning without touching the buffer, which is the same class of fault as
`TimeFormat` above. Wiring a real library in later means replacing that function, not extending
it; the project already has an iTunesDB parser (§11.4).

#### The five only TWA reaches

TWA does not boot, so none of these has ever executed, and none is given behaviour a trace could
not confirm. `AsyncFileIO #10` is a Tracker release (`0x0029d6e0`). `#9` queues a four-parameter
operation. **`#7` does `and r1, r0, #0xff` before `0x001e3b48`** — a mode in the low byte of
argument 0 is exactly how `#0` and `#3` open a file (§19), so it is a fourth open variant; it is
deliberately *not* wired to the file layer, because which register carries the path cannot be read
off the shim and guessing would hand the game a handle to the wrong file rather than no handle.
`OpenGLES #160` (`0x0026b214`) allocates a pair of 0x1c-byte descriptors per slot in
`0x1084bb44`/`0x1084bb84` and copies a program image in: **uploading a custom pipeline** into one
of eight user slots, the counterpart to `#159` selecting one of the fifty built-ins (§17).
`#168` runs its arguments through the double-precision soft-float library, so it builds a matrix
in doubles; which matrix is not established.

#### The three left unimplemented, and why

`miscTBD #2`, `OpenGLES #3` and `OpenGLES #10` are reached only through an orphan veneer — **no
binary contains a call site for any of them**. They are real functions (`#10` validates against
`GL_ARRAY_BUFFER`, so it is a buffer-object entry; `#3` takes a bound `< 0x20`), but giving
behaviour to code that never runs is inventing it. Left as they are, and recorded here so the
count of 158/161 is not mistaken for an oversight.

### 18.0.3 The button flags word, found for nine more titles

Not an ordinal, but it came out of the same tool and it is worth more than several of them.

Buttons only ever worked for Minigolf, because Minigolf is the one title whose flags word was
measured by hand (`0x18037a0c`). Every other title printed *"no button flags word known for this
title — wheel works, buttons do nothing"* unless someone passed `--flags-addr=`. §13.5 established
that buttons arrive two different ways; this is the half that needs an address.

The address has a signature. Minigolf's poll does:

```asm
18018a18  ldr r0, [r9, #0x14]
18018a1c  cmp r6, #1            ; NOT adjacent — an adjacency-only matcher finds nothing, anywhere
18018a20  bic r0, r0, #0x60     ; clear ONLY the two wheel bits
18018a24  str r0, [r9, #0x14]
```

Masking a word with `0x60` and writing it straight back is what lets a button bit set elsewhere in
the frame survive into dispatch, and nothing else in these binaries does it. Resolving the base
from the nearest preceding `ldr rN,[pc,#imm]` gives `literal + offset`.

**It reproduces `0x18037a0c` for Minigolf** — the control that makes the rest believable — and
finds one for nine titles that had no buttons at all:

| title | flags word | | title | flags word |
|---|---|---|---|---|
| Minigolf *(control)* | `0x18037a0c` | | Pac-Man | `0x180ac89c` |
| Bejeweled | `0x180c1358` | | Tetris | `0x180256d0` |
| Zuma | `0x180b8f08` | | Texas Hold'em | `0x180597a8` |
| Cubis 2 | `0x180a9db0` | | Vortex | `0x18063e5c` |
| Mahjong | `0x18049e60` | | TWA | `0x1806fee4` |

Eight titles have no such pattern — testprep, mspacman, SimsBowling, SimsPool, Lost, molly, Sudoku
and Solitaire — and that is an answer rather than a miss: those take buttons as event-list nodes
instead. Sudoku is the clean case, and it explains a stall that looked like a missing ordinal:
over 150 frames it executes 32 329 instructions and reaches **only** `InputEvents #0`,
`miscTBD #0` and `miscTBD #9`. It opens no file, makes no GL call and draws nothing. It is polling
for an input event that never arrives, and no amount of framework coverage was ever going to move
it.

Measured over 950 frames, a Select press every 25 frames, against the same run with no input:

| title | quads, no input | quads, with buttons |
|---|--:|--:|
| Tetris | 3 561 | **25 275** |
| Cubis 2 | 8 050 | **33 659** |

**This, and not the 81 ordinals, is what moved a game.** §18.0.4 says so plainly.

### 18.0.4 What closing the 81 actually changed: nothing, and that is the finding

Measured, not assumed. `EAPP_AUDIT_SKIP=audio,gl,misc,metadata,twa` leaves the whole batch
unimplemented, so the same binary can run a title both ways. Every one of the eighteen titles was
run for 950 frames with a Select press every 25 frames, both ways:

```
title                     quads off/on        instructions off/on
Bejeweled                    210/210          10275750/10275750
Cubis 2                    33627/33659        19958586/19967657
Texas Hold'em                  1/1             1523219/1523219
LOST                           0/0            12117368/12117428
Mahjong                     2838/2838          5579760/5579760
Mini Golf                   6901/6901         34052153/34056415
PAC-MAN                    31751/31720        16498206/16521120
The Sims Bowling               0/0            16517032/16516850
Sudoku · Solitaire · Ms. PAC-MAN · LOST · musika · Vortex · iQuiz · SAT Prep · Zuma — identical
```

The three rows that differ do so by 0.1% or less and in both directions; input is scheduled by
frame and these titles are timing-sensitive, so that is jitter, not effect. **No title boots
further, renders more, or stops spinning because of the 81.**

That is worth stating flatly because the opposite was the reasonable expectation. The explanation
is in §18.0.2: for most of the 81, zero was already the right answer — the sound-descriptor
setters have no call-time effect, the music-player half of Audio talks to a task that does not
exist, `glDisable` only ever switches off features this rasteriser lacks, and Metadata's empty
library reports what a zero already reported. The genuine corrections (`miscTBD #10` = 1000,
`Settings #0("TimeFormat")` writing a terminator instead of leaving uninitialised stack, the
transport state that makes `Audio #39` answerable) are real, and none of them is on the path any
title is currently stuck on.

What the work is worth is different from what it was expected to be worth: the framework surface
is now **fully characterised** rather than partly guessed, every remaining stall is provably
*not* a missing ordinal, and the audit is a tool that stays true instead of a number that decayed.
Where the games are actually stuck, from the same runs:

| title | reaches | stuck on |
|---|---|---|
| Sudoku, Solitaire, Ms. PAC-MAN | `InputEvents #0`, `miscTBD #0`/`#9` only | input — no flags word, event-node path not wired (§18.0.3) |
| Bejeweled | 21 GL entries, `AsyncFileIO #3` but never `#2` | opened a file, waiting on a read that never completes; spins at `0x18011c18` |
| Texas Hold'em | 17 GL entries, dies after 1 frame | spins across `0x18001adc..0x1802e2b4` |
| The Sims Bowling / Pool | no GL at all | the null-pointer writes to page 0 at `0x18022dc0` |
| LOST | 12 M instructions, 0 quads | §12a — the renderer is cold two call levels up |

### 18.1 Method, and the static scan (OpenGLES, six titles — historical)

Every title's OpenGLES descriptor sits at file `0x2c` (pointer at header `+0x10`), with
`count = 0xB3 = 179` at `+0x30`, **179 thunks at `+0x38` (`VA 0x18000064`)** and the resolved
pointer array at `0x18000330`. Identical in all six binaries — measured, not assumed:

| title | file | desc | count | thunk[0] |
|---|---|--:|--:|---|
| Minigolf | `Games_RO/88888/.../Minigolf_1_1_2563296.bin` | `0x2c` | 179 | `0x18000064` |
| Bejeweled | `Games_RO/55555/.../Bejeweled_1_1_2563296.bin` | `0x2c` | 179 | `0x18000064` |
| Zuma | `Games_RO/44444/.../Zuma_1_1_2563298.bin` | `0x2c` | 179 | `0x18000064` |
| Pac-Man | `Games_RO/AAAAA/.../Pacman_1_1_2563976.bin` | `0x2c` | 179 | `0x18000064` |
| Tetris | `Games_RO/66666/.../Tetris_1_1_2563292.bin` | `0x2c` | 179 | `0x18000064` |
| Lost | `Games_RO/1B200/.../Lost_1_1_2917525.bin` | `0x2c` | 179 | `0x18000064` |

Scanning every ARM `B`/`BL` whose target lands in `0x18000064..0x1800032f`, plus one-instruction
`b <thunk>` veneers and their callers, plus a literal-pool search for thunk and resolved-slot
addresses (no title uses an indirect reference — the literal count is zero everywhere):

| title | statically reachable ordinals (call sites) |
|---|---|
| Minigolf | 4(9) 12(3) 13(4) 19(1) 21(1) 36(8) 37(4) 40(8) **53(24)** **84(1)** 99(1) **101(1)** 125(1) 137(8) 157(1) 158(1) 159(5) 165(2) 167(1) **175(3)** |
| Bejeweled | 4(22) 12(2) 13(3) 21(1) 36(12) 37(25) 40(39) 45(2) **84(3)** 99(6) **105(1)** 125(20) 137(39) 148(21) 157(1) 158(1) 159(20) 165(31) 167(7) **169(9)** **171(4)** **173(4)** **175(1)** |
| Zuma | 4(17) 12(2) 13(3) 36(10) 37(19) 40(32) 45(2) **84(2)** 99(3) **105(1)** 125(17) 137(32) 148(16) 157(1) 158(1) 159(17) 165(26) 167(3) **169(14)** **171(5)** **173(7)** **175(7)** |
| Pac-Man | 4(2) 12(3) 13(2) **35(2)** 36(3) 37(6) 38(2) 40(6) 99(2) 125(1) 137(16) 157(1) 158(1) 159(2) 167(1) |
| Tetris | 4(2) 12(3) 13(3) **35(1)** 36(2) 37(1) 40(4) 45(1) 99(1) 125(1) 137(4) 148(1) 157(1) 158(1) 159(1) 165(3) 167(1) **169(49)** **173(1)** **175(1)** |
| Lost | **0(1v)** 4(1) 12(2) 13(2) 19(1) 37(1v/9) 40(16) 99(1) 137(16) **147(6)** **149(11)** 152(2) 153(3) 157(1) 159(1) 164(2) |

(`v` = the count is veneers; Lost's `#37` veneer at `0x7340` has nine callers, §12.1. The Lost and
Pac-Man rows reproduce §12.1 and §15.7 exactly, which is the scan's own cross-check.)

**Every runtime-observed ordinal appears in its title's static set.** There is no ordinal reaching
the stub layer that the binary cannot call, so the tracer is not mis-attributing anything, and the
§15.7 discrepancy ("Pac-Man calls `#45`/`#148`/`#165`") is now resolved in favour of the binary —
the current Pac-Man trace no longer claims them.

### 18.2 The answer: twelve missing ordinals (superseded by §18.0 — now seven)

Union of all six static sets minus the 24 implemented:

```
0  35  53  84  101  105  147  149  169  171  173  175
```

Split by whether a runtime trace has already hit them:

| | ordinals |
|---|---|
| **observed at runtime AND missing** | `35` (Pac-Man, Tetris) · `53` (Minigolf) · `84` (Minigolf, Bejeweled, Zuma) · `101` (Minigolf) · `169` (Bejeweled, Tetris) · `171` (Bejeweled) · `173` (Bejeweled, Tetris) · `175` (Minigolf, Bejeweled, Tetris) |
| **statically referenced, never observed** | `0` (Lost) · `105` (Bejeweled, Zuma) · `147` (Lost) · `149` (Lost) · plus `169`/`171`/`173`/`175` in **Zuma** and `105` in both — screens the trace has not reached |

Lost's three (`0`, `147`, `149`) are unobserved for the reason §12a gives: Lost never enters its
renderer, and all three live inside the draw blocks. They are not optional — they are downstream of
the bug we are still chasing.

### 18.3 What each one is

Named from `opengles-names.json` where it has an entry, otherwise from the disassembly. `#101`'s
name is new here and comes from position: `#99 glTexImage2D`, `#100 glTexImage3D`, **`#101`…`#104`**,
`#105 glTexSubImage2D` — the only alphabetical fill is `glTexParameterf`, `fv`, `i`, `iv`, and the
bodies agree (they all funnel into one shared `glTexParameter` validator).

| # | addr | name | what it does |
|--:|---|---|---|
| 0 | `0x26c534` | `glActiveTexture` | validates `0x84C0..0x84C2`, stores the unit index at `ctx+0x8C` |
| 35 | `0x26d6e8` | `glDisable` | clears one of nine capability bytes in `ctx+0x278..0x280`, emits one packet |
| 53 | `0x26ea10` | `glGetError` | reads and zeroes `ctx+0x88` |
| 84 | `0x26f4fc` | `glPixelStorei` | stores `GL_PACK_ALIGNMENT` at `ctx+0x264`, `GL_UNPACK_ALIGNMENT` at `ctx+0x268` |
| 101 | `0x2703f4` | `glTexParameterf` | validates; acts **only** on `GL_TEXTURE_PRIORITY` |
| 105 | `0x27054c` | `glTexSubImage2D` | uploads a sub-rectangle of pixels into the bound texture |
| 147 | `0x271678` | `glUniform4xAPPLE` | writes one 16.16 `vec4` constant register from four scalars |
| 149 | `0x27234c` | `glUniformMatrix4xvAPPLE` | writes a `mat4` into four consecutive constant registers, 16.16 |
| 169 | `0x275004` | `mat4 translatef` | `m[12..15] += m[0..3]*x + m[4..7]*y + m[8..11]*z` |
| 171 | `0x274dfc` | `mat4 scalef` | column 0 `*= x`, column 1 `*= y`, column 2 `*= z` |
| 173 | `0x27434c` | `mat4 rotatef` | rotate about `(x,y,z)` by `angle` degrees |
| 175 | `0x273db8` | `mat4 multMatrixf` | `dst = a × b`, column-major |

#### `#0 glActiveTexture` — measured

```asm
26c534:  sub  r0, r0, #0x8000
26c53c:  sub  r0, r0, #0x4c0        ; r0 = unit - GL_TEXTURE0
26c540:  cmp  r0, #2
26c548:  strls r0, [r1, #0x8c]      ; ctx+0x8C = active unit
26c54c:  popls {r4, pc}
26c550:  mov  r0, #0x500 ; str r0,[r1,#0x88]   ; GL_INVALID_ENUM
```

Three units, `0x84C0..0x84C2`. Binding slots are `ctx[0x94 + 4*unit]` (§12.1).

#### `#35 glDisable` — measured, and the full cap table

The dispatch at `0x26d6f4..0x26d774` is a chain of `sub`/`subs` compares; each arm clears one byte
and, **only if the byte was previously non-zero**, emits a packet whose 16-bit tag is a hardware
state register:

```asm
26d790:  ldrb r0,[r1,#0x278]     ; GL_CULL_FACE
26d794:  cmp  r0,#0
26d798:  beq  0x26d888           ; already disabled -> emit nothing at all
26d79c:  ldr  r0,[pc,#272]       ; 0x00000292 = the register tag
26d7a0:  strb r2,[r1,#0x278]     ; = 0
```

| cap | enum | `ctx` byte | tag |
|---|---|---|---|
| `GL_CULL_FACE` | `0x0B44` | `+0x278` | `0x292` |
| `GL_POLYGON_OFFSET_FILL` | `0x8037` | `+0x279` | `0x294` |
| `GL_SCISSOR_TEST` | `0x0C11` | `+0x27A` | `0x296` |
| `GL_SAMPLE_COVERAGE` | `0x80A0` | `+0x27B` | `0x298` |
| `GL_SAMPLE_ALPHA_TO_COVERAGE` | `0x809E` | `+0x27C` | `0x299` |
| `GL_STENCIL_TEST` | `0x0B90` | `+0x27D` | `0x2A0` |
| `GL_DEPTH_TEST` | `0x0B71` | `+0x27E` | `0x2A4` |
| `GL_BLEND` | `0x0BE2` | `+0x27F` | `0x2AA` |
| `GL_DITHER` | `0x0BD0` | `+0x280` | `0x2AB` |

Anything else is `GL_INVALID_ENUM`. `#39 glEnable` (`0x26e240`) is the mirror image.

#### `#101 glTexParameterf`, and the fact that filters are not settable on this driver

`0x2703f4` converts `r2` to an integer, calls the shared validator at `0x00107a58`, and then:

```asm
270428:  sub  ip, r5, #0x8000
27042c:  subs ip, ip, #0x66         ; pname == 0x8066 GL_TEXTURE_PRIORITY ?
270430:  bne  0x2704e0              ; no  -> return, having stored NOTHING
270434:  ... clamp r4 into [0.0f, 1.0f] ...
27048c:  ldr  r1,[r0,#0x8c]         ; active unit
270494:  ldr  r0,[r0,#0x94]         ; bound texture name
270498:  add  r0,r0,#0x200
27049c:  add  r0,r0,#0x49           ; tag = 0x249 + texture name
2704a0:  strh r0,[sp,#18]
2704a8:  ... float -> ldexp(.,16) -> int ...   ; 16.16
2704dc:  bl   0x27f3b8
```

The validator `0x00107a58` is **pure** — it returns 1 or panics, and stores nothing:

```asm
107aa4:  cmp  r1,#0x2800            ; GL_TEXTURE_MAG_FILTER
107aa8:  subne ip,r1,#0x2800
107aac:  subsne ip,ip,#1            ; GL_TEXTURE_MIN_FILTER
107ab4:  ldr  ip,[pc,#152]          ; 0x00002701
107ac4:  cmp  r2,#0x2600            ; GL_NEAREST / GL_LINEAR / the four mipmap modes
107b10:  ... GL_REPEAT 0x2901 / GL_CLAMP_TO_EDGE 0x812F / GL_MIRRORED_REPEAT 0x8370
107b2c:  mov  r0,#1
107b30:  pop  {r4,pc}
```

So **`GL_TEXTURE_MIN_FILTER`, `MAG_FILTER`, `WRAP_S` and `WRAP_T` are validated and thrown away.**
They are not stored in the context and never reach the hardware. That is an independent
confirmation of §16.1: sampling is fixed-function bilinear and coordinates are texels
(`GL_ARB_texture_rectangle`, §15.1) because there is no path by which a game could ask for
anything else.

Worth recording for anyone who trips over it later: **`#102`/`#103`/`#104` are hard aborts.**

```asm
270520:  push {r4,lr}
270524:  bl   0x107a58          ; returns 1 or panics
270528:  cmp  r0,#0
27052c:  popeq {r4,pc}          ; never taken
270530:  bl   0x7ca24           ; 0x7ca24 = panic (0x7df0c(1,0) then b 0x25f77c)
```

`glTexParameteri` cannot be called on this device at all. Same for `#1`, `#2`, `#34`, `#81`, `#82`
(`0x26c584`, `0x26c588`, `0x26d6e4`, `0x26f42c`, `0x26f430` — each a lone `bl 0x7ca24`). Those are
*not* "returns 0" no-ops; an emulator that answers 0 is more permissive than the device.

#### `#105 glTexSubImage2D` — nine arguments, measured slot by slot

`0x27054c` pushes 13 registers then `sub sp,#76`, so incoming stack arguments start at `sp+128`:

```asm
27054c:  push {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,sl,fp,lr}
270550:  sub  sp,sp,#76
270558:  ldr  r7,[sp,#128]      ; width
27055c:  ldm  fp,{r5,r8,fp}     ; fp = sp+132 -> height, format, type
27056c:  ldr  sl,[sp,#144]      ; pixels
270590:  sub  r1,r8,#0x1906 ; cmp r1,#4        ; format in ALPHA..LUMINANCE_ALPHA
2705b4:  ... target must be 0x0DE1 or 0x84F5, and < 0x8517
2705d8:  bl   0xe3158           ; bytesPerRow(width, format, type)  -- the SAME rule as #99
2705fc:  mul  r0,r5,r0          ; height * rowBytes
270628:  add  r0,r0,#40         ; + a 40-byte parameter block
270664:  ldr  r0,[r0,#0x94]     ; the bound texture on the active unit
27067c:  bl   0x27f180          ; split across the ring, payload verbatim
```

```c
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void *pixels);
```

The parameter block is `{target, level, 0, xoffset, yoffset, width, height, format, type}`.
**`glPixelStorei` is not consulted here either** — `0xe3158` takes only `(width, format, type)`, so
`rowBytes = bpp * width` with no padding, exactly as for `#99` (§12.8).

Bejeweled and Zuma share one texture-upload helper, and this is where it branches (Bejeweled
`0x1a324`, Zuma `0x1cf94` — byte-identical code):

```asm
1a324:  ldr r0,[r6,#28]
1a328:  cmp r0,#0
1a32c:  bne 0x1a368            ; already created -> SUB-image update
1a330:  mov r1,#1 ; sub r0,r7,#0xec
1a338:  bl  0x1b4              ; #84 glPixelStorei(0x0CF5 GL_UNPACK_ALIGNMENT, 1)
1a360:  bl  0x1f0              ; #99 glTexImage2D(...)
...
1a368:  ldr r3,[r6,#52]        ; pixels
1a37c:  mov r3,#0 ; mov r2,#0 ; mov r1,#0        ; yoffset, xoffset, level
1a388:  stm sp,{r8,fp}         ; width, height
1a374:  add r7,sp,#8 ; stm r7,{r4,r5}            ; format, type
1a370:  str r3,[sp,#16]                          ; pixels
1a38c:  bl  0x208              ; #105
```

`r7 - 0xec` with `r7 = 0x0DE1` is `0x0CF5`, i.e. `GL_UNPACK_ALIGNMENT` — measured, and it pins the
`#84` argument at the same time.

#### `#147 glUniform4xAPPLE` — five arguments, same register bank as `#148`

`0x271678`. §12.7 inferred this; it is now measured.

```asm
271678:  push {r4,lr}
27167c:  sub  sp,sp,#40
271680:  ldr  ip,[sp,#48]        ; the FIFTH argument, w
271684:  cmn  r0,#1
271688:  beq  0x27170c           ; location == -1 -> silent no-op
27168c:  add  r4,sp,#24
271690:  stm  r4,{r1,r2,r3,ip}   ; x, y, z, w  -> a 16-byte payload
2716b0:  cmp  r0,#4
2716c0:  ldrcs lr,[pc,#76]       ; 0x00000101
2716d0:  subcs r0,r0,#4
2716d4:  movcc lr,#1
2716d8:  add  r0,lr,r0
2716dc:  strh r0,[sp,#14]        ; tag = location<4 ? 1+loc : 0x101+loc-4
271708:  bl   0x27f3b8
```

```c
void glUniform4xAPPLE(GLint location, GLfixed x, GLfixed y, GLfixed z, GLfixed w);
```

Identical tag arithmetic to `#148` and `#120` (§15.3). It is the scalar form of the **per-draw
modulate colour**. Lost's six call sites are `0x5200`, `0x20f04`, `0x3b4b8`, `0x3b690`, `0x3b7d8`,
`0x3b9c8` — one per draw block, immediately before `#149`.

#### `#149 glUniformMatrix4xvAPPLE` — the 16.16 twin of `#125`

`0x27234c`, `(GLint location, GLsizei count, GLboolean transpose, const GLfixed *value)` in
`r0..r3`. Same `-1` no-op, same `count < 0 -> GL_INVALID_VALUE`, same tag arithmetic, then four
emits per matrix at `tag, tag+1, tag+2, tag+3` with payloads `value+0/16/32/48`:

```asm
2723ac:  cmp  r5,#4
2723bc:  ldrcs r8,[pc,#572]      ; 0x00000101
2723d0:  subcs r5,r5,#4
2723d4:  movcc r8,#1
2723d8:  cmp  lr,#0              ; transpose
2723e4:  bne  0x2725d4           ; a separate transposing emit path exists
2723f4:  add  r6,r8,r5
272400:  strh r6,[sp,#78]
27240c:  bl   0x27f3b8           ; column 0
272410:  add  r0,r6,#1 ; add r3,r4,#16
272434:  bl   0x27f3b8           ; column 1
272438:  add  r0,r6,#2 ; add r3,r4,#32     ; column 2, then +3 / +48
```

**Lost uses `#149` and never `#125`.** Eleven call sites (`0x5af4`, `0x5be8`, `0x7dfc`, `0x8790`,
`0x8b34`, `0x1615c`, `0x20eec`, `0x3b4a0`, `0x3b678`, `0x3b7bc`, `0x3b9ac`).

#### `#169`/`#171`/`#173`/`#175` — argument registers, measured

§15.4 named these; the prologues pin the arguments:

```asm
275004:  push {...}                      ; #169 translatef
27500c:  mov r4,r0 ; mov r7,r1 ; mov r5,r2 ; mov r6,r3
275010:  ldr r0,[r0,#32]                 ; m[8]
275020:  bl  0x2a8ac4                    ; __aeabi_fmul  m[8]*z
275038:  ldr r0,[r4]                     ; m[0] * x, then m[4] * y, summed into m[12]

274dfc:  mov r4,r0 ; mov r7,r1 ; mov r5,r2 ; mov r6,r3    ; #171 scalef
274e04:  ldr r0,[r0]     ; m[0] *= x
274e1c:  ldr r0,[r4,#16] ; m[4] *= y
274e2c:  ldr r0,[r4,#32] ; m[8] *= z
274e3c:  ldr r0,[r4,#4]  ; m[1] *= x   ...

27434c:  mov r4,r0 ; mov r0,r1          ; #173 rotatef
274358:  sub sp,sp,#44
27435c:  ldr r9,[sp,#80]                ; the FIFTH argument, z
274360:  ldr r1,[pc,#1360]              ; pi/180 at 0x2748b8
274374:  bl  0x7dd5c                    ; sin
274380:  bl  0x7d69c                    ; cos

273db8:  mov r6,r0 ; mov r4,r1 ; mov r5,r2        ; #175 multMatrixf(dst, a, b)
273dc4:  ldr r0,[r4,#48] ; ldr r1,[r2,#12]        ; a[12]*b[3]
273dd8:  ldr r0,[r4,#32] ; ldr r1,[r5,#8]         ; a[8]*b[2]
273de8:  ldr r0,[r4,#16] ; ldr r1,[r5,#4]         ; a[4]*b[1]
273df8:  ldr r0,[r4]     ; ldr r1,[r5]            ; a[0]*b[0]  -> dst[0]
```

```c
void translatef(GLfloat m[16], GLfloat x, GLfloat y, GLfloat z);       // r0..r3
void scalef    (GLfloat m[16], GLfloat x, GLfloat y, GLfloat z);       // r0..r3
void rotatef   (GLfloat m[16], GLfloat angle, GLfloat x, GLfloat y, GLfloat z);  // r0..r3, sp+0
void multMatrixf(GLfloat dst[16], const GLfloat a[16], const GLfloat b[16]);     // r0, r1, r2
```

All four are pure: they read and write only the caller's matrices, call only soft-float, and touch
no context. `dst = a × b` in the OpenGL column-major sense.

### 18.4 Ranked by whether ignoring it can produce a visible fault

#### Rank 1 — `#175 multMatrixf`. Live, non-deterministic bug in Minigolf today.

Minigolf has **exactly one** `#125 glUniformMatrix4fv` call site, and the matrix it uploads is
produced by `#175` **into a stack frame**:

```asm
; Minigolf 0xeaa0
eaa4:  push {lr}
eaa8:  sub  sp,sp,#68             ; a 64-byte matrix at sp+4, UNINITIALISED
eaac:  add  r0,sp,#4              ; dst
eab0:  sub  r1,r2,#68             ; a
eab4:  bl   0x320                 ; #175 multMatrixf(dst, a, b)
eab8:  add  r3,sp,#4
eabc:  mov  r2,#0 ; mov r1,#1 ; mov r0,#0
eac8:  bl   0x258                 ; #125 glUniformMatrix4fv(0, 1, 0, dst)
```

With `#175` returning 0 and writing nothing, `Stub::GlUniformMatrix` reads
`f32::from_bits(mem[dst + 20])` out of **whatever was last on that stack page**. It then sets
`self.proj_flips_y = true` if the value happens to be negative — a sticky, one-way flag. The
visible fault is Minigolf's entire screen rendering **upside down, intermittently, depending on
stack contents**. This is the single highest-value fix in the list and it is twenty lines of
arithmetic.

Bejeweled (`0x1df88 -> 0x1df9c`) and Zuma (`0x24224 -> 0x24238`) have the same shape, into globals
rather than stack, so they read a zeroed matrix — element 5 is `+0.0`, no flip, but the matrix is
still wrong the moment the renderer starts applying it.

```asm
; Bejeweled 0x1df7c  (Zuma 0x24218 is identical)
1df7c:  ldr r2,[pc,#92]   ; b   = 0x180bead0
1df80:  ldr r1,[pc,#92]   ; a   = 0x180bea90
1df84:  ldr r0,[pc,#92]   ; dst = 0x180beb10
1df88:  bl  0x320         ; #175
1df8c:  ldr r3,[pc,#84]   ; 0x180beb10
1df9c:  bl  0x258         ; #125 glUniformMatrix4fv(0, 1, 0, dst)
1dfac:  bl  0xf8          ; #37 glDrawArrays(7, 0, 4)
```

This bears on §13.6's "its `glUniformMatrix4fv` is the identity", which is true of **six** of
Bejeweled's twenty `#125` sites and not of the other fourteen. The six are immediately preceded by
`#165 loadIdentity` (`0x6228 -> 0x623c`, `0xe2f0 -> 0xe304`, `0x1cfac -> 0x1cfc0`,
`0x1d440 -> 0x1d454`, `0x1dad4 -> 0x1dae8`, `0x1dc40 -> 0x1dc54`). The other fourteen are the
opposite order — upload, then `#165` as a trailing reset (`0x82b4 -> 0x82bc`, `0x9230 -> 0x923c`,
`0x9468 -> 0x9474`, `0xab98 -> 0xaba4`, `0xb8d0 -> 0xb8dc`, `0xda80`, `0xdc6c`, `0xdfcc`,
`0x1099c`, `0x14d4c`, `0x174cc`, `0x1798c`, `0x1857c`, `0x1df9c`) — and what they upload is the
accumulated matrix at `0x180bea90`, built through a current-matrix pointer that `#167` **and**
`#169`/`#171`/`#173` all write:

```asm
; Bejeweled 0x8f88 / 0x8fa0 -- same target, dereferenced from the current-matrix pointer in r5
8f88:  bl 0x300          ; #167 ortho(*r5, ...)
8f94:  ldr r0,[r5]
8fa0:  bl 0x308          ; #169 translatef(*r5, x, y, 0)
```

So `--flip-y` is still required for the six identity uploads, and for the fourteen the emulator is
today seeing an **ortho-only** matrix — correct in its Y sign (because `#167` is implemented) but
missing every translate, scale and rotate. That the traces reported "the identity" is consistent
with the six; whether any of the fourteen was in the trace is not established here.

#### Rank 2 — `#105 glTexSubImage2D`. A texture's pixels silently never arrive.

Bejeweled and Zuma take this branch whenever `[r6+28] != 0`, i.e. **the second and later uploads
into a texture name that already exists**. Ignoring it means the first upload is the only one that
ever lands: an atlas that is created empty and then filled, or a background patched in after
creation, stays at whatever it was created with. This is precisely the open lead §15.6 left for
Zuma's missing 322×222 background ("`#105 glTexSubImage2D` … is live in Zuma and appears in no draw
log quoted so far"), and it is now the only one of those three leads that is a plain missing entry
point rather than a modelling question.

#### Rank 3 — `#149 glUniformMatrix4xvAPPLE`. Lost has no other matrix path at all.

Eleven call sites, one per draw block, and Lost never calls `#125`. Every projection Lost sets is
therefore invisible to the emulator: `proj_flips_y` is never evaluated for it, and if the renderer
is ever taught to apply the MVP, Lost will be the one title with no MVP. The fault is
"Lost's geometry is drawn with an implicit identity, and its Y direction is a coin flip".

#### Rank 4 — `#147 glUniform4xAPPLE`. Every tinted Lost quad renders white.

Same constant-register bank as `#148`, and §16.2 already measured what dropping that bank costs:
"Ignoring it painted every such fill white — which is exactly what buried Zuma's menu and level art
under a white screen." Lost calls `#147` six times, once per draw block, and never `#148`. The
moment Lost reaches its renderer, ignoring `#147` reproduces Zuma's white screen exactly.

#### Rank 5 — `#169 translatef`, `#171 scalef`, `#173 rotatef`. Latent, but Tetris is all of it.

Tetris calls `#169` from **49 sites**, all of the form

```asm
; Tetris 0x8034
8038:  ldr r7,[pc,#584]   ; &currentMatrix
8040:  ldr r0,[r7]        ; the matrix
8044:  mov r3,#0          ; z = 0
8048:  bl  0x308          ; #169 translatef(m, x, y, 0)
```

— a 2D placement stack. Bejeweled and Zuma drive the same three in their draw blocks
(`#169 -> #173 -> #171 -> ... -> #175 -> #125`, Bejeweled `0x1ddec`/`0x1de3c`/`0x1de60`).

These have **no visible effect today** only because `Stub::GlUniformMatrix` deliberately does not
apply the matrix — the titles hand `glDrawArrays` screen-space vertices. They become rank-1 the
instant the renderer starts transforming by the MVP: geometry at the wrong place, wrong size and
wrong rotation, with no other symptom. They also feed `#175`, so a correct `#175` on top of no-op
translate/scale/rotate is only half a fix. Implement all four together.

#### Rank 6 — harmless to stub as `0`, with the reason

These four are the ones the audit clears, and it is worth being explicit about why, because three
of them *look* dangerous:

* **`#53 glGetError` — a `0` return is not a stub, it is the correct answer.** The whole function is

  ```asm
  26ea10:  ldr r1,[pc,#12]      ; 0x1084bbc4
  26ea14:  mov r2,#0
  26ea18:  ldr r0,[r1,#0x88]
  26ea1c:  str r2,[r1,#0x88]
  26ea20:  bx  lr
  ```

  With no error pending it returns 0. Minigolf's 24 call sites are assertion checks; answering 0 is
  answering "no error". Reproducible exactly, at zero cost.
* **`#84 glPixelStorei` is inert on this driver.** It writes `ctx+0x264`/`+0x268` and *nothing in
  any path these games use ever reads them*: `#99 glTexImage2D` (§12.8) and `#105 glTexSubImage2D`
  (18.3) both compute `rowBytes` from `0xe3158(width, format, type)` alone, and `#87 glReadPixels`
  — the only plausible consumer of `GL_PACK_ALIGNMENT` — is called by none of the six. Measured
  arguments: Minigolf `glPixelStorei(0x0D05 GL_PACK_ALIGNMENT, 1)` at `0xa354`; Bejeweled/Zuma
  `glPixelStorei(0x0CF5 GL_UNPACK_ALIGNMENT, 1)`. Both are the default-equivalent anyway.
* **`#101 glTexParameterf` cannot set a filter or a wrap mode**, because this driver stores
  neither (18.3). Minigolf's single call is
  `glTexParameterf(0x84F5, 0x8066 GL_TEXTURE_PRIORITY, 0.0f | 1.0f | ratio)` — literals at
  `0x16310 = 0x84F5` and `0x16318 = 0x8066`, with `r2` selected at `0x1624c`/`0x16244`. That is a
  texture-residency hint for a driver LRU that the emulator does not have. Dropping it is exact.
* **`#35 glDisable` is called only with capabilities that are already off.** `0x000ce0fc` bzeroes
  `ctx` for `0x284` bytes and then sets exactly two bytes, `+0x274` and `+0x280`:

  ```asm
  ce104:  mov r1,#0x284 ; bl 0x7ccd0     ; bzero(ctx, 0x284)
  ce128:  strb r1,[r0,#0x274]
  ce12c:  strb r1,[r0,#0x280]            ; only GL_DITHER starts enabled
  ```

  so blend, depth, cull, scissor, stencil all start **disabled**, and `#35` emits nothing at all
  when the byte is already zero. **No title calls `#39 glEnable`** — the static scan finds zero
  sites in all six. The three measured `#35` calls are `glDisable(GL_DEPTH_TEST)` (Pac-Man
  `0x2dd4`, literal `0x2e30 = 0x0B71`), `glDisable(GL_CULL_FACE)` (Pac-Man `0x43c0`, literal
  `0x4408 = 0x0B44`) and `glDisable(GL_CULL_FACE)` (Tetris `0x552c`,
  `mov r0,#0x344; add r0,r0,#0x800`). All three are redundant on a fresh context.

  The one caveat, recorded so it is not rediscovered: **`#159` toggles `GL_DEPTH_TEST` internally.**
  `0x26afc4: tst r0,#0x10000 / bl 0x26e240 (#39) / bl 0x26d6e8 (#35)` with the literal at
  `0x26b100 = 0x0B71`. So depth testing on this device is a property of the selected pipeline, not
  of anything the game says.
* **`#0 glActiveTexture` is single-unit in practice.** Lost wraps it at `0x71d0` and does track two
  units in its own state (`str 0/1, [0x18060910+0xb34]`), but all four call sites pass
  `GL_TEXTURE0`: literals `0x5a48`, `0x7ec4`, `0x8bf4` are each `0x000084c0`, and the fourth
  (`0x5e04`) restores a value that Lost had itself read back through its *own* shadow
  `glGetIntegerv` at `0x7380` (which answers from `[0x18060910+0xb34]` and never calls the driver).
  A single global binding slot is therefore sufficient. If a future title passes `0x84C1`, the
  fault would be a bind for unit 1 clobbering unit 0 — the wrong texture on every subsequent draw.

### 18.5 Implementation notes

Registers are AAPCS; every "stack argument at `sp+N`" below is at the callee's entry `sp`, which is
what the stub layer sees.

**`#175 multMatrixf` — `r0 = dst`, `r1 = a`, `r2 = b`, all `float[16]` column-major.**
`dst[c*4+r] = Σ_k a[k*4+r] * b[c*4+k]`. Compute into a local 16-float array first: Minigolf passes
`dst` overlapping neither input, but Bejeweled's `dst`/`a`/`b` are three separate globals and
nothing guarantees that in general. Write all 16 words back. Returns void. After writing, run the
same `element 5 < 0 -> proj_flips_y` test the `#167` stub already does, because for Minigolf this
is the only matrix that ever exists.

**`#169 translatef` — `r0 = m`, `r1..r3 = x, y, z` as IEEE bit patterns.**
`for r in 0..4: m[12+r] += m[0+r]*x + m[4+r]*y + m[8+r]*z`. Nothing else changes. Returns void.

**`#171 scalef` — `r0 = m`, `r1..r3 = x, y, z`.**
`m[0..3] *= x; m[4..7] *= y; m[8..11] *= z`. Row 3 (`m[12..15]`) untouched. Returns void.

**`#173 rotatef` — `r0 = m`, `r1 = angle` (degrees), `r2 = x`, `r3 = y`, `[sp+0] = z`.**
Normalise `(x,y,z)`, build the standard `glRotatef` 3×3 and post-multiply `m` by it in place
(`m = m × R`). `sin`/`cos` at `0x7dd5c`/`0x7d69c`, `pi/180` literal at `0x2748b8`. Returns void.

**`#105 glTexSubImage2D` — `r0 = target`, `r1 = level`, `r2 = xoffset`, `r3 = yoffset`,
`[sp+0] = width`, `[sp+4] = height`, `[sp+8] = format`, `[sp+12] = type`, `[sp+16] = pixels`.**
Route it through the same decoder `#99` already uses: `rowBytes = bpp(format, type) * width` with
no alignment and no padding, `height * rowBytes` bytes read verbatim from `pixels`, expanded
`(L,L,L,A)` / `(L,L,L,255)` / `(0,0,0,A)` / 565 / 4444 / 5551 / RGB / RGBA as §12.8's table says.
Blit the decoded rectangle into the texture currently bound on the active unit at
`(xoffset, yoffset)`, leaving the rest of the image alone. Accept `0x0DE1` and `0x84F5`
identically. Returns void. Reject nothing the games do — `level` is always 0 here.

**`#149 glUniformMatrix4xvAPPLE` — `r0 = location`, `r1 = count`, `r2 = transpose`, `r3 = value`,
values are 16.16 fixed.** `location == -1` is a silent no-op. Read `16*count` words, divide each by
65536.0, and feed exactly the same store `#125` uses. A draw consumes it the way `#125`'s output is
consumed today (currently: element 5's sign only). If `transpose != 0`, transpose before storing —
the driver has a separate path for it at `0x2725d4`, though no title measured here sets it.

**`#147 glUniform4xAPPLE` — `r0 = location`, `r1 = x`, `r2 = y`, `r3 = z`, `[sp+0] = w`, all 16.16
fixed.** `location == -1` is a silent no-op. Convert each by `/ 65536.0` and store into the same
constant register `#148` writes: `reg = location < 4 ? 1 + location : 0x101 + location - 4`. When
`location == 4`, that is `self.modulate`, latched per draw, and the draw does
`fragment = texel * modulate` clamped to `[0,1]` — identical to `#148` (§16.2). Returns void.
Sharing one code path with `Stub::GlUniform4x` is the right shape; only the argument gathering
differs (four registers instead of a pointer).

**`#0`, `#35`, `#53`, `#84`, `#101`** — leave them returning 0. If cheap fidelity is wanted:
`#53` should return and clear a stored error word (it is already exact while no error is set);
`#0` should store the unit index and have `glBindTexture` key on it; `#35` should clear a
nine-entry capability array whose reset value is "all zero except `GL_DITHER`". None of these
changes a pixel for the six titles measured.

### 18.6 Faults found in the current implementation

* **`Stub::GlUniformMatrix` reads uninitialised memory for Minigolf.** Covered in 18.4 rank 1. It
  is not the stub that is wrong — it is that its input has no producer. Until `#175` exists, the
  cheapest mitigation is to skip the `proj_flips_y` test when the 16 words do not look like a
  matrix (e.g. `m[3]`, `m[7]`, `m[11]` not all zero, or `m[15] != 1.0f`), which every matrix this
  library produces satisfies.
* **`#158` returns `0x3000` unconditionally.** Measured (§15.5): the driver returns `0x300C` for a
  `pname` outside `0x3F000..0x3F005`. Both callers pass `0x3F001`, so this is currently
  unobservable — recorded, not urgent.
* **`#159` discards its index.** ABI-correct (`1` is what `0x26a9e8` returns on every path,
  including the out-of-range default at `0x26ae9c`), but §15.7 shows Pac-Man's wrapper at `0x4414`
  enabling/disabling *attribute 2* per index, i.e. the index decides whether the per-vertex colour
  array is live, and 18.4 adds that it also toggles `GL_DEPTH_TEST`. This is a modelling gap, not a
  wrong argument.
* **`Stub::GlUniform4x` latches only `location == 4`.** Correct for `count == 1`, which is all any
  title uses; a `count > 1` starting at 4 would fill `0x101, 0x102, …` and only the first is kept.
* **Everything else checks out against the disassembly.** Verified argument-by-argument:
  `#120 glUniform4fv` is `(r0 location, r1 count, r2 value)` — `0x271344: mov r8,r1 / 0x271370: mov
  r4,r2` — matching `GlUniform4x { fixed: false }`. `#125` takes `value` in `r3`
  (`0x271e0c`, and §12.7's own call site). `#137 glVertexAttribPointer` is
  `(r0 index, r1 size, r2 type, r3 normalized, [sp+0] stride, [sp+4] pointer)` — `0x273058: add
  r5,sp,#56 / ldm r5,{r1,r5}` after `push{r4,r5,r6,lr}; sub sp,#40`, with `index < 16` and
  `1 <= size <= 4` enforced at `0x273074`. `#99` and `#19` read `width` from `r3` and
  `height`/`border`/`format`/`type`/`pixels` from `sp+0/4/8/12/16`, which is what the stubs do.
  `#152` writes `1` and `2` through the pointers in `r1` and `r2`, matching `0x26b1ec`/`0x26b1f4`.
  `#167`'s stub reproduces `0x27356c` exactly, including `m[10] = 2/(zNear-zFar)` and
  `m[14] = (zFar+zNear)/(zNear-zFar)`.

### 18.7 What is not established

* **Whether `#105` is what is missing Zuma's 322×222 background.** It is now the leading candidate
  by elimination (§15.6 ruled out the other five entries; the stale-name and pipeline-index leads
  remain open), but nothing here ran.
* **What the `#159` pipeline indices compute**, unchanged from §12.11 and §15.10.
* **Whether `#173 rotatef` post- or pre-multiplies.** The disassembly at `0x27434c` builds `sin`,
  `cos` and the normalised axis and then writes the matrix; the accumulation order was not traced
  instruction by instruction. `m = m × R` is the `glRotatef` convention and is what `#169`/`#171`
  are measured to follow, so it is inference by consistency. Bejeweled and Zuma call it between a
  `translatef` and a `scalef` on the same matrix, which only makes sense post-multiplied.
* **`#149`'s transpose path** at `0x2725d4` was not read; no title sets `transpose != 0`.
* Nothing in this section was executed. The runtime ordinal lists quoted in 18.2 come from the
  emulator's own traces; everything else is static reading of `osos.bin` and the six eApps.

## 19. Request operations beyond open and read

`AsyncFileIO`'s request carries an operation type at `+0x04`. §2 established **6 = open** and noted
a sibling requiring **7**. Three more turned up, all sent through the ordinal we bind to "read",
and all of them mis-handled because the emulator judged every request by "did we move `len` bytes".

| type | seen in | shape | meaning |
|---|---|---|---|
| 3 | Test Prep | `len` set, **buffer NULL** | seek/skip |
| 5 | Mahjong | `len` 0, **buffer NULL** | a bufferless operation on an open file |
| 6 | all | buffer + length | open (§2) |

### 19.1 A bufferless request is not a failed read

The status write was `if got == len && got > 0 { 0 } else { !0 }`. For a request that transfers
nothing that is **always failure**, and the games reissue it forever. Mahjong sends a type-5
request with a null buffer and zero length 1 700 times in 1 500 frames and never leaves its
loading screen; reporting success for a zero-length operation gets its progress bar from stuck to
**100%**.

### 19.2 A length with no buffer is a SEEK

Test Prep's type-3 requests carry `len = 4` and a **null** buffer: it wants the position advanced
past a four-byte header, not four bytes delivered. Treating it as a read wrote those bytes to
address **zero** and left the game re-opening `Fonts/Roman/ArialBold12.blob` indefinitely.
`seek_file` advances the position and transfers nothing.

Neither change was sufficient on its own — both titles still stall — but both were writing to bad
addresses or reporting false failures, and the working titles are unaffected.

### 19.3 Writing files, at last

A **write-mode open (mode 1) whose request carries a buffer is a save**, and the bytes are now put
on disk. Sudoku opens `savefile.dat` with mode 1 and a 17 228-byte buffer every frame; the file it
writes is exactly that size. This is the write half of the open-mode discovery in §13.2 and the
mechanism Minigolf's missing save would use.

### 19.4 The `Lost(0)` cluster, characterised

Sudoku, SimsBowling and SimsPool all die the same way: a **virtual call through a null object**
(`ldr r0,[r4] / ldr r1,[r0,#0x58] / bx r1`), and in every case the heap sits at **~5.24 MB**.

That number is the games' own doing, not ours. Sudoku's allocator wrapper at `0x18007918` asks
`miscTBD #0` for **ten blocks of `0x7FF80` (524 160) bytes** — 5.24 MB exactly — and sub-allocates
from them. `miscTBD #1`, our free, has **zero call sites in the binary**: nothing is ever returned.
The game then re-creates a ~10 KB screen object every frame (its global at `0x1805ab58` takes a new
pointer each time, stepping by `0x2900`) until the pool is dry and its own allocator answers null.

So the fault is a state machine looping, not memory management. Ruled out by measurement: our
allocator never refuses (64 MB, no refusals logged); `miscTBD #5` and `#6` are not frees (binding
either as one changes nothing and the heap figure is identical); `#6` is not a memory report
(answering it with 32 MB changes nothing). None of these titles draws a single quad.

### 19.5 The cause: PP5022 IRAM was never mapped on the eApp path

Sudoku keeps a state flag in **on-chip IRAM at `0x4000003d`** and writes it with
`strb r7,[r4]` at `0x180313dc`. The eApp memory map had only the image, RAM at `0x11000000`, the
heap at `0x19000000` and scratch — `0x40000000` was unmapped, so that store went nowhere and the
flag read back as zero forever.

The flag gates the per-frame branch at `0x18031398`: zero means "run initialisation", which builds
a fresh ~10 KB screen object. So the game re-initialised on **every frame** until its own ten
512 KB pool blocks were exhausted and its allocator answered null — the `Lost(0)` crash.

Mapping 128 KB at `0x40000000` fixes it. **Sudoku and Solitaire no longer crash**; both now run
indefinitely (they sit idle at ~130 instructions per frame rather than progressing, which is a
different and much smaller problem). SimsBowling still faults, but from a different cause: it
writes through **null pointers** 6 672 times into page 0 before dying, so its null originates
earlier than the fatal call.

The viewer now reports unmapped WRITES per page with the hottest PC, since a dropped store is
invisible by construction — the value simply reads back as zero — and this class of bug cost four
titles.

## 20. The frame-reason byte is a handshake, and that is what boots The Sims

`play --frame-reason=auto`. The Sims Bowling and The Sims Pool both went from **zero quads and a
`Lost(0)` crash** to their full title screens with this one change.

### 20.1 What the byte is

RetailOS's frame pump is `0x0024dadc`, and it is three instructions of intent:

```asm
0024dadc  ldrb r0,[r4,#0x230]    ; the eApp manager's own state
0024dae0  cmp  r0,#1
0024dae4  moveq r0,#5            ; state 1 -> reason 5
0024daec  cmp  r0,#3
0024daf0  bne  0x0024dafc        ; anything else -> DO NOT WRITE THE BYTE AT ALL
0024daf4  mov  r0,#4             ; state 3 -> reason 4
0024daf8  strb r0,[r4,#0x00]
0024dafc  add  r1,r4,#0x100      ; and the game is called as f(ctx, ctx+0x100)
0024db08  bx   r5
```

So `ctx+0x00` is a **reason** the OS asks with, `ctx+0x100` is the **answer** the game writes back,
and in most states the OS leaves the reason where the game left it. The first call sees the zero
the manager was allocated with.

The Sims Bowling's dispatcher at `0x18045740` reads it, and the three values it acts on are
measured:

| reason | what the game does |
|--:|---|
| `0` | drain the event list, then run the **full application init** at `0x180052d4` |
| `1` | the normal per-frame path (`0x18045794`) |
| `5` | the suspend/resume path, which answers **6** |

and `0x1804578c` writes **1** into `ctx+0x100` once the init is done.

### 20.2 Why a constant is fatal

The emulator wrote the byte **once**, before the init vectors, and never again.

* Held at **0**, the game runs its whole application init on every frame. It never destroys what
  the previous one built — the replace site at `0x18019e24` destroys `[owner+0x1c]` first, but the
  owner is itself fresh each time, so its slot is always null. 75 constructions, 0 destructions.
* Held at **1**, the init never runs at all and the game sits idle at 2.3 M instructions.

Each construction takes about 54 KB — 25 000 + 13 276 + 3 072 + 3 072 + 2 256 + 1 596 + 1 536 and
a tail — and the game's heap is **fixed at 5.24 MB by design**: ten 524 160-byte blocks it takes
from `miscTBD #0` once, sized by its own constant `0x7FF78` at `0x18005af8` and a count of ten at
`0x18007a00`. It never asks for more. At iteration ~76 the 3 072-byte allocation at `0x18022d84`
returns 0, `[obj+0x78]` stays null, and the loop at `0x18022dbc` writes 768 words through it —
those are the 6 672 writes to page 0 recorded as the `Lost(0)` cluster. The jump to zero that
follows is a virtual call on a screen pointer that is null for the same reason.

**The null writes and the jump to zero were never the bug.** They are the last two symptoms of a
handshake that was answered with a constant.

### 20.3 The rule, and where it does not apply

Ask for init until the game says it is done, then ask for frames — `0` until `ctx+0x100` is
non-zero, then `1`. Measured over all eighteen titles, 950 frames, Select every 25 frames:

| title | quads without | quads with |
|---|--:|--:|
| The Sims Bowling | 0 *(crash at frame 157)* | **780** |
| The Sims Pool | 0 *(crash at frame 539)* | **930** |
| SAT Prep Reading | 9 | **1 861** |
| Zuma | 12 400 | **15 538** |

Twelve titles are unchanged. **Two get worse: LOST and Vortex**, both of which drive the byte
themselves — §12 records Lost writing it back from its own state at `0x1803d844` — so the flag has
to stay opt-in until that case is modelled. `auto:N` sets the steady value; 1 is the only one that
renders, and 2, 3, 4 and 5 were all tried.

An ownership heuristic ("stop writing once the byte is not what we last wrote") does **not**
separate the two groups: The Sims Bowling writes 1 into the reason byte itself at frame 1, exactly
as Lost does. Recorded because it is the obvious next idea and it does not work.

### 20.4 The `.rlb` resource library is one blocker under five titles

Exactly five titles ship a `.rlb` library, and they are exactly the five that do not play:

| title | library | size | where it stops |
|---|---|--:|---|
| The Sims Bowling | `gameLib.rlb` | 20 MB | title screen, bar animating |
| The Sims Pool | `gameLib.rlb` | 20 MB | title screen, bar animating |
| Mahjong | `main.rlb` | 7.3 MB | loading screen at 89% |
| Sudoku | `Sudoku.rlb` | 16.4 MB | nothing drawn |
| Royal Solitaire | `Solitaire.rlb` | — | nothing drawn |

Mahjong gets furthest and shows the mechanism. It opens `main.rlb` **bufferless** — `buf = 0`,
`len = 0` — and the open's completion at `0x1801da7c` sets the file object's state byte to 2, the
same "ready" convention §12.10 records for Lost. It then streams with op **4** requests built by
`0x18021744`, whose buffer and length come from `[lib+0x10c]` and `[lib+0x118]`.

**Both are zero.** The path that fills them is `0x18016aa0`, and it never runs; the constructor at
`0x180169f4` zeroes them and nothing else writes them. So every read moves nothing, the completion
handler at `0x18016d5c` re-issues from the same two zero fields, and Mahjong spins — about 1 700
identical requests in 1 500 frames — on its loading screen.

Two things were tried and are recorded because they are the obvious ideas:

* **Answering the bufferless open with the file's size** in `[req+0x24]`, where a load already
  reports its byte count. The size arrives (7 292 512 for `main.rlb`) and changes nothing — the
  game does not read it there. Kept: it costs nothing and a bufferless open has no other question.
* **Not completing a read with neither buffer nor length** (`EAPP_DROP_EMPTY_READS=1`). This does
  kill the spin, and moves Mahjong exactly zero frames. Off by default: over all eighteen titles it
  is neutral everywhere except Zuma, which drops from 15 538 quads to 9 860.

### 20.5 Inside the `.rlb` reader: op 5, and the gate that cannot open itself

Going a level deeper into Mahjong pins the loop down exactly.

**Op 5 does not use the buffer fields at all.** Its builder is `0x18021678`, and it writes
`[req+0x04] = 5`, `[req+0x0c] = value`, `[req+0x10] = mode`, and **zeroes `+0x14` and `+0x18`**.
So the "read with no buffer and no length" in the log is not a degenerate read — it is an
operation whose payload lives in two fields the emulator does not look at. RetailOS's submit at
`0x001e36c8` accepts exactly ops **3, 4 and 5**, and dispatches them on `[req+0x2c]` rather than
the file object at `+0x08`.

Mahjong drives its library with `mode = 4` (`[lib+0x110]`, set at `0x18018f78`) and a count in
`[lib+0x114]`. Its completion path at `0x18016ce8` reads:

```asm
18016ce8  ldr  r1,[r4,#0x114]   ; the outstanding count
18016cec  cmp  r1,#0
18016cf0  bne  0x18016d00       ; non-zero -> RE-ISSUE op 5   <-- the loop
18016cf4  ldrb r2,[r4,#0x110]   ; zero and mode 2 -> issue the op-4 read
```

so the loop ends only when the count reaches zero, and the reader only asks for data once the
**gate** `[lib+0x124]` is open. Every path that opens the gate is itself gated:

| site | opens the gate | but first requires |
|---|---|---|
| `0x18016f00` | `[slot+0x124] = 1` | reached only from a posted callback |
| `0x18016e74` | posts that callback | `0x18016ec8` — i.e. the gate already open |
| `0x18016b68` | issues op 5 | the same |
| `0x18016c6c` | `[lib+0x124] = 1` | `[lib+4] == 0` |

The one entry that is *not* self-gated is the completion at `0x18016c3c`:

```asm
18016c50  ldr  r0,[r4,#0x174]   ; = [fileobj+8], the operation's RESULT
18016c54  str  r0,[r4,#0x11c]
18016c60  ldrb r1,[r4,#0x04]
18016c68  bne  0x18016c98
18016c6c  strb r0,[r4,#0x124]   ; the gate opens
```

`lib+0x174` is `fileobj+8` — the field an open leaves the **handle** in. Reading a stale handle
back as a byte count is a real defect, and it is now fixed: every async operation overwrites
`[obj+8]` with what it actually transferred. Measured across all eighteen titles: zero
regressions. It does **not** move Mahjong, because the callback that reads it (`0x18016c3c`, four
arguments) is not the one the op-5 requests carry (`0x18016948`, two arguments) — so the gate is
still shut by a different route.

### 20.6 The operation table, from RetailOS's own dispatcher — and §19 was wrong

The worker is `0x001e3764`. It takes the job, reads `[req+0x04]`, and jumps through the table at
`0x001e3788`. Anything outside the table sets `[req+0x20] = 0x1f` and fails. This is the authority,
and it **corrects the field map §19 carried**:

| op | handler | what it is | arguments |
|--:|---|---|---|
| 3 | `0x001e3d90` | **write** | len `+0x18`, buf `+0x14`, bytes written -> `+0x24` |
| 4 | `0x001e3e2c` | **read** | len `+0x18`, buf `+0x14`, bytes read -> `+0x24`, new position -> `+0x28` |
| 5 | `0x001e3db8` | **seek** | offset `+0x0c` **sign-extended** (`mov r1,r2,asr #31`), whence `+0x10` |
| 6 | `0x001e3860` | open | — |
| 7, 8, 9, 10, 0x64 | — | further ops, not yet read | — |

§19 recorded "3 = seek, 5 = bufferless". Both halves were wrong, and the second one is why five
titles stall: **op 5 is a seek**, and the emulator was treating it as "a read with no buffer",
which is to say as nothing at all. All three ops resolve their stream through `[req+0x2c]`, not
the file object at `+0x08`.

The old reading of op 3 survives contact, though, and it is worth saying why rather than leaving
it looking like a second bug. §19 measured Test Prep sending op 3 with `buf = 0, len = 4` and
needing the file position advanced by four. A **write** of four bytes advances the position by
exactly four — so "advance by `len`" was the right behaviour for the wrong reason, and the code
that did it is left in place for ops outside 3..=5.

The emulator now dispatches on the op byte the way the worker does, and implements a real seek
with C whence semantics (0 set, 1 current, 2 end — the three values `0x001e3dc8` checks against).
Measured across all eighteen titles: zero regressions.

### 20.7 What is still shut

Mahjong now issues a correct seek and gets a correct answer, and still does not progress: the
seeks are all `offset 0, whence 0` because `[lib+0x114]` and `[lib+0x110]` are zero, and the
op-4 reads that alternate with them carry `[lib+0x10c]`/`[lib+0x118]`, also zero. The reader is
idling correctly — **nothing has asked it for data**.

Asking requires the gate `[lib+0x124]`, and §20.5's table stands: every path that opens the gate
is reached only through the gate, except the completion at `0x18016c3c`, which the op-5 requests
do not carry. So the library is not stuck on the file layer any more; it is stuck waiting for its
own consumer to start, and that consumer is above the resource system rather than inside it.

### 20.8 The pump's two bytes are used in opposite roles by different titles

RetailOS calls the game as `f(ctx, ctx+0x100)` and writes its reason into `ctx+0x00`. Two titles
built on what is visibly the same engine — the timeout constants `0x3d0900` (4 s) and `0x1e8480`
(2 s), the `mov r10,#5`, the identical answer stores — disagree about which byte is which:

| title | reads the reason from | writes its answer to |
|---|---|---|
| The Sims Bowling | `ctx+0x00` (`0x18045740: ldrb r0,[r5,#0]`) | `ctx+0x100` (`0x1804578c`) |
| Sudoku | `ctx+0x100` (`0x180311f4: ldrb r0,[r4,#0]`) | `ctx+0x100`, then mirrors it into `ctx+0x00` at `0x180314b0` |

`play --reason-offset=0x100` drives the other byte, and the answer is then read from the one not
being driven. It changes nothing for Sudoku — 128 659 instructions against 636 498 when the byte
is left alone — so **zero is what Sudoku wants there**, and it is already zero. Sudoku is not
blocked on the handshake; with reason 0 it takes the same normal-frame path The Sims Bowling takes
with reason 1, drains an empty event list, and runs 684 instructions a frame doing nothing.

Recorded because the two titles look identical at a glance and are not, and because the obvious
next move — "drive the other byte" — is measured and does not work.

### 20.9 State of the five, and what each is actually waiting for

| title | reaches | waiting on |
|---|---|---|
| The Sims Bowling | title screen, 842 quads | its resource consumer to start |
| The Sims Pool | title screen, 930 quads | the same |
| Mahjong | loading screen, 89% | the same — the `.rlb` reader now seeks correctly and idles correctly |
| Sudoku | nothing, 684 instr/frame | stops finding new code at instruction 14 032 of 636 498 |
| Royal Solitaire | nothing | the same |

The file layer is no longer the blocker for any of them. Op 5 is a seek and is implemented; the
open reports a size; `[obj+8]` carries a real result. What is left is one level up: nothing asks
the resource system for a resource.
