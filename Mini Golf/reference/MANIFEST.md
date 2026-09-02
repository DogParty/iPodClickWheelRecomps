# Reference snapshot

Copies of the emulator-tree files this recomp was written against. The emulator is under
active development by other people and agents, so the plan and the code cite **these copies**,
not the live tree. To refresh: re-copy, update this manifest, and re-check every citation.

- Snapshot taken: 2026-08-20T20:52Z
- ipod-emulator commit: `f28c21364c459ca847909dc7201a3a0bddc5e340` (2026-08-18); the eapp-loader
  sources below include **uncommitted** working-tree edits (`--callgraph-dump` in play.rs, covscan.rs).
- Game image: `Minigolf_1_1_2563296.bin`, 227868 bytes, sha256 `5a80bf1a0d595a7be65c5e4bdda580a7bc608e781598a7165f95d8dfcff9bab4` (not copied — it is the user's game data)

| file | from | sha256 |
|---|---|---|
| `eapp-loader/lib.rs` | `tools/eapp-loader/src/lib.rs` | `eaa133f7274e324a…` |
| `eapp-loader/play.rs` | `tools/eapp-loader/src/bin/play.rs` | `e4f92461375238a3…` |
| `eapp-loader/trace.rs` | `tools/eapp-loader/src/bin/trace.rs` | `41a97056cd65289d…` |
| `eapp-loader/covscan.rs` | `tools/eapp-loader/src/bin/covscan.rs` | `0307d0d37dac234c…` |
| `eapp-loader/dis.rs` | `tools/eapp-loader/src/bin/dis.rs` | `20ea902ee7e552f7…` |
| `eapp-loader/Cargo.toml` | `tools/eapp-loader/Cargo.toml` | `7b255da5753a265b…` |
| `arm7tdmi/arm.rs` | `tools/arm7tdmi/src/arm.rs` | `b73d0f1b4cf3e2a3…` |
| `arm7tdmi/bus.rs` | `tools/arm7tdmi/src/bus.rs` | `6108b5a557568b63…` |
| `arm7tdmi/cpu.rs` | `tools/arm7tdmi/src/cpu.rs` | `757193c48d93f25f…` |
| `arm7tdmi/disasm.rs` | `tools/arm7tdmi/src/disasm.rs` | `d1014108e3713db9…` |
| `arm7tdmi/lib.rs` | `tools/arm7tdmi/src/lib.rs` | `10f6c28d3056b398…` |
| `arm7tdmi/thumb.rs` | `tools/arm7tdmi/src/thumb.rs` | `0b1905dead0ec27b…` |
| `reversing/asyncfileio-abi.md` | `../reversing/asyncfileio-abi.md` | `9b6aebbb88122f04…` |
| `reversing/framework-functions.json` | `../reversing/framework-functions.json` | `869120b6be4fcc1f…` |
| `reversing/opengles-names.json` | `../reversing/opengles-names.json` | `e8ac02aab69271f5…` |
| `research/13-do-the-games-load.md` | `research/13-do-the-games-load.md` | `2a8794d45cb2adb1…` |

## What each copy is used for

- `eapp-loader/lib.rs` — the spec for `src/libeapp/`: the `Stub` enum is the behaviour of every framework ordinal; `Machine::draw_arrays` is the rasteriser to port; `REQ_CALLBACK`/`REQ_CONTEXT` are the AsyncFileIO request offsets; `EApp::parse` is the header layout.
- `eapp-loader/play.rs` — the frame pump, input flags word, script format, and key map the runtime copies.
- `eapp-loader/trace.rs`, `covscan.rs`, `dis.rs` — the instruments the analysis numbers came from.
- `arm7tdmi/*.rs` — `add_with_carry`, `shift_imm`, `shift_reg`: the flag and barrel-shifter rules the emitter ports verbatim.
- `reversing/asyncfileio-abi.md` — commentary on the file-I/O ABI.
- `reversing/framework-functions.json`, `opengles-names.json` — ordinal → name tables used to name the `ipod_eapp.h` functions.
- `research/13-do-the-games-load.md` — wheel detents per row (§2.2) and how the titles boot.
