// Corrections applied to the game's own shipped data as it is read.
//
// This header exists for one defect, and the bar for adding a second is the same as the bar for
// this one: the fault must be *in the data*, established by reading the game's own code, and the
// correction must be derivable from the file rather than hard-coded at an offset.
//
// **The defect.** The three glyphs on the ENTER NAME wheel — backspace, space and done — are
// entries in the game's texture pack (`tex`) whose 16-byte header declares 32x32 at *16* bits a
// pixel while the entry carries only 1024 bytes: exactly 32x32 at *8*. The game's texture
// uploader at `0x18015628` reads the bits-a-pixel from byte 9 of that header and dispatches on
// it — 8 selects `GL_ALPHA` (or `GL_LUMINANCE` for format code 7), 16 selects `GL_RGB` with
// `GL_UNSIGNED_SHORT_5_6_5` — so it hands the driver a 32x32 5-6-5 upload, 2048 bytes, over a
// 1024-byte source. Half of each tile is therefore the *next* pack entry's bytes read as colour,
// and the half that is "right" is an 8-bit mask misread as 5-6-5. Read as the 8-bit alpha mask
// it is, the same 1024 bytes are a clean glyph.
//
// The emulator draws it wrong too, and so does this port without the correction, because both
// faithfully do what the game asks. Nothing in the renderer can tell the difference: the call is
// well formed, and the bytes past the end of the entry are real memory.
//
// **What is corrected.** One byte per mislabelled entry — the header's bits-a-pixel, 16 -> 8 —
// and only where the entry's own length proves the label wrong: the data is exactly one byte a
// pixel where the header claims two. Every other entry in the pack is self-consistent and is
// left alone. Nothing is resized, moved, or re-encoded, so every offset the game computes is
// unchanged.
//
// **When it is not applied.** The oracle runs with `--emulator-firmware`, which turns this off:
// the recordings in `tests/expected/` were made by the emulator, which does not correct
// anything, and a corrected run makes a *different framework call* (`GL_ALPHA` where the
// recording has `GL_RGB`). So the correction is off wherever the recordings are the standard,
// and on for anyone playing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vortex::gamedata {

// Whether the corrections below are applied. On by default; `--emulator-firmware` turns them off
// (runtime/main.cpp), because the recorded oracles are of an uncorrected run.
void set_asset_corrections(bool enabled);
[[nodiscard]] bool asset_corrections_enabled();

// Correct the contents of the file the game opened as `name`, in place. Returns how many
// corrections were made — 0 for every file this knows nothing about, which is all but one.
uint32_t correct_asset(const std::string& name, std::vector<uint8_t>& bytes);

}  // namespace vortex::gamedata
