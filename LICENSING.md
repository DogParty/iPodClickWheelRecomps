# Licensing

Two licences, split along one directory boundary.

| What | Where | Licence |
|---|---|---|
| The tools | `common/tools/` | MIT (`common/tools/LICENSE`) |
| Everything else | the six titles, `common/src/`, `common/cmake/` | GPL-3.0-or-later (`LICENSE`) |

Copyright (C) 2026 Brandon Smith. This page and `common/tools/LICENSE` are the only places in
the tree that name an author.

## Why the split

`common/tools/` is the recompiler and the build machinery: it reads a binary you supply and
writes C++, a Windows executable or a macOS .app. None of it is part of a shipped game, and
none of it contains or embeds game code. MIT means anyone can build on it, including for a
seventh iPod title, without inheriting copyleft. That is the same split N64: Recompiled uses,
where the recompiler is MIT and the port that comes out of it is GPL-3.0.

Everything that ends up inside a game a person downloads is GPL-3.0-or-later: the titles, and
the shared runtime in `common/src/` that is linked into all of them. It is the licence the
recompilation scene has settled on for ports, and it means a fork that is handed to other people
comes with its source.

"or later" lets anyone use a future version of the GPL as well, so the project is not stranded
if version 3 is ever superseded.

## What the licence does not cover

**The games themselves.** These titles are EA's. Nothing in this repository is their code, art,
music or levels, and no release contains any of it: a player supplies their own copy, taken off
their own iPod. What is here is a port that can run that copy — hand-written host code, a
hand-decompilation of the game's logic, and for five of the six titles a machine translation of
the game's own binary that is generated on the builder's machine and never committed (see
`RELEASING.md`).

A licence governs the part that is ours to license. It cannot and does not grant any right in
the original games, which remain their owner's.

## Third-party code

Nothing third-party is vendored in this tree. What a build links, and what a release therefore
carries, is listed in `common/licenses/README.md`; the notice each one asks to travel with is
beside it, and `common/tools/release.sh` copies them into every artifact that needs them.

## Applying the notice to a source file

There are no per-file licence headers. The two `LICENSE` files and this page are the statement,
which is how the rest of this scene does it and is enough for the licences to apply. If you ever
want headers, the line to add is `// SPDX-License-Identifier: GPL-3.0-or-later` (or
`MIT` under `common/tools/`), and it wants doing mechanically rather than by hand.
