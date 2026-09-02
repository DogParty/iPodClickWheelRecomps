// Menu labels this port adds, which no resource pack has a string for.
//
// Every menu row names its text with a resource id that the course pack resolves
// (`resource_load` in menu_render.cpp, and again in menu.cpp to measure the row's width). The
// rows this port adds — the Cheats screen and the Options row that opens it — say things the
// 2008 resource packs have no word for in any of their eleven languages. So their text comes
// from here instead: a text id with the top bit set is not a pack resource but one of the
// labels below, and `host_text_load` writes it into the same scratch buffer `resource_load`
// would have filled, in the same encoding, so nothing downstream can tell the difference.
//
// The top bit is safe to spend: pack resource ids are small indices into a course pack's
// directory (the largest the game names is 0x7e), and `MenuItem::text_id` is a full word.
//
// The labels are ASCII and are not translated. Every one of the game's fonts indexes its glyph
// advances from the space (`text_width`, menu.cpp), so ASCII draws correctly in the UTF-16
// language too; what it cannot do is speak Japanese, and a row this port added reads in English
// there. Translating them would mean shipping text beside the game's own, which is a bigger
// decision than a cheats menu should make on its own.
#pragma once

#include <cstdint>

namespace minigolf::game {

// The labels this port adds, in no particular order — the enumerator is the id, not a position
// in any menu.
enum class HostText : uint32_t {
    CheatsRow,      // the Options row that opens the Cheats screen
    CheatsTitle,    // that screen's own title
    UnlockCourses,  // one per cheat, each reading "NAME: ON" or "NAME: OFF"
    NoStrokeLimit,
    NoOutOfBounds,
    AimGuide,
    GhostTrail,
};

// Set on a text id to mark it as one of the labels above rather than a pack resource.
constexpr uint32_t HOST_TEXT_FLAG = 0x8000'0000u;

[[nodiscard]] constexpr uint32_t host_text_id(HostText text) {
    return HOST_TEXT_FLAG | static_cast<uint32_t>(text);
}

[[nodiscard]] constexpr bool is_host_text(uint32_t text_id) {
    return (text_id & HOST_TEXT_FLAG) != 0;
}

// Write `text`'s label into guest memory at `destination`, terminated, as UTF-16 or as bytes
// according to the language the game is running in — exactly what `resource_load` leaves behind.
void host_text_load(uint32_t text_id, uint32_t destination);

// The same for a string this port composes itself. Exposed because the score card and the
// statistics page build their lines rather than naming them.
void host_text_write(uint32_t destination, const char* ascii);

// Show only what the iPod's game showed: the Options menu without its Cheats row (menu.cpp),
// the Statistics page without this port's numbers under the original's (page.cpp).
//
// Both of the project's oracles compare this build against something that predates the port —
// `tests/diff.sh` against call logs recorded from the emulator, `tests/vs-recomp.sh` against the
// pure recompilation of the original code — and a row or a line this port added is a real
// difference that both are right to see. Rather than teach either oracle to overlook it, the
// additions are taken away for the comparison: `--emulator-firmware` sets this, and so does
// `--no-port-additions`, which is what vs-recomp.sh gives the decompiled side so that the two
// builds are drawing the same game. Nothing else should set it — a player who cannot reach the
// Cheats screen cannot turn a cheat off again either.
void set_port_additions_hidden(bool hidden);
[[nodiscard]] bool port_additions_hidden();

}  // namespace minigolf::game
