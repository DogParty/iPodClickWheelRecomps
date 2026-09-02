# Mini Golf interactive sessions — coverage log

Baseline before the sessions: 5 scripted probes (boot, title, name entry) reached 191/304 game
functions, 8 413 / 20 942 instructions.

## Run 1 — 3 390 frames, 80.9 M instructions, 2 272 distinct edges
- 209/304 game functions live in this run; **18 new** vs every earlier run (2 909 insns).
- New code is menu/UI-flow: 0x18005f54 (435), 0x1800c1dc (433), 0x18010c3c (402),
  0x18012fa8 (364), 0x180051d0 (state-table switch, 166), 0x1800c8d0, 0x18006688, 0x18010654,
  0x18006ce4, 0x18004b4c, 0x18007be8, 0x1800d058, 0x18009198, 0x18012ed4, 0x1800553c,
  0x1800cfa8, 0x180054bc, 0x180104bc.
- Imports reached unchanged from the probes plus AsyncFileIO #12/#14 (the name-confirm path).
- **Course renderer 0x1800a080, main state machine 0x18002c28, physics 0x1800d1c4 / 0x180154bc
  still never executed** — no hole was played.
- Cumulative: 209/304 functions, 11 322 / 20 942 insns (54%).

## Run 2 — 28 005 frames, 2.90 G instructions, 19 clears, 3 248 distinct edges — a real game
- 235/304 game functions live; **47 new** (8 014 insns). Gameplay reached: course renderer
  0x1800a080 (2092), main state machine 0x18002c28 (1354), physics 0x1800d1c4 (612) and
  0x180154bc (517), 0x1801197c, 0x18014734, 0x1800de88, 0x180109cc, 0x180043f4, 0x18010280,
  0x1800e868, 0x18005980, 0x18015258 and ~35 smaller ones (0x18009788..0x18009d50 cluster =
  per-object draw/animation helpers).
- New imports vs run 1: OpenGLES #21 glCopyTexImage2D, #84 glPixelStorei, #99 glTexImage2D
  (runtime texture uploads during play); Audio #53 (volume). AsyncFileIO #12/#14 NOT called this
  run — the route the player took did not go through the name-confirm save path.
- Gameplay costs ~100 k instructions/frame vs ~27 k on the name-entry screen.
- Cumulative: **256/304 functions, 19 336 / 20 942 insns (92%)**. Remaining 48 are small
  (biggest 0x18018b88 174 insns, 0x18006f7c 154) — pause menu / options / end-of-round paths.
