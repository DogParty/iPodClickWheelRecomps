// The cheats this port adds, and the rule about records that goes with them.
//
// The iPod game had none of this. These are the port's, and they live where a player would look
// for them: Options ▸ Cheats (menu.cpp installs the row, cheats_menu.cpp is the screen).
//
// Two decisions hold the feature together, and both are here rather than spread across the
// screens that read them:
//
//  * **A cheat is a choice, not a change to the saved game.** Unlocking the courses does not
//    write to the 0x144-byte record the iPod wrote and this port still writes byte for byte
//    (records.h, `SaveRecord`) — it widens the gate the course carousel reads
//    (`courses_available`). Turning the cheat off gives back exactly the progress that was
//    earned, and a save file carried to another build of the game is untouched. The flags live
//    in their own file in the platform's save store, beside the settings and the key bindings.
//
//  * **A cheat that changes the rules voids the round's records.** `round_records_void()` is
//    true from the moment such a cheat is on during a round, and `hole_finish` (hole_tick.cpp)
//    then leaves the best round, the statistics and the round history alone. Without this a
//    single toggle would quietly rewrite the numbers on the statistics page and the "BEST
//    ROUND" under every course picture, and there would be no way to tell which of them were
//    honest.
//
// The ghost trail is deliberately not one of the rule-changing cheats. It draws the player's
// own path on a hole they have already finished, so it tells them nothing about a hole they
// have not, and a round played with it still counts. It sits on the same screen because that is
// where this port's toggles are, not because it is a cheat.
#pragma once

#include <cstdint>
#include <string>

namespace minigolf::game {

enum class Cheat : uint32_t {
    UnlockCourses,  // every course selectable, however few have been earned
    NoStrokeLimit,  // the ten-stroke limit raised out of the way
    NoOutOfBounds,  // a ball that leaves the green comes back without the penalty
    AimGuide,       // the aim line follows the power meter while it swings
    GhostTrail,     // the best round's path on this hole, drawn faintly
};

constexpr unsigned CHEAT_COUNT = 5;

// Every cheat, in the order the Cheats screen lists them.
[[nodiscard]] const Cheat* all_cheats();

// What the row says. The screen adds ": ON" or ": OFF" (host_text.cpp).
[[nodiscard]] const char* cheat_label(Cheat cheat);

// The name in the saved file — stable, never shown to a player.
[[nodiscard]] const char* cheat_key(Cheat cheat);

// Whether turning this one on stops the round it is used in from counting.
[[nodiscard]] bool cheat_voids_records(Cheat cheat);

[[nodiscard]] bool cheat_enabled(Cheat cheat);

// Turn one on or off. Enabling a rule-changing cheat voids the round in play at once, so that
// switching it on for one awkward hole cannot be switched off again before the card is written.
void cheat_set(Cheat cheat, bool enabled);
void cheat_toggle(Cheat cheat);

// The courses the carousel may offer: what the save record says has been earned, or all three
// with the cheat on. The progression itself (`hole_finish`) reads the record, never this, so
// cheating a course open does not also award it.
[[nodiscard]] uint32_t courses_available();

// The stroke limit in force on a hole. Without the cheat this is the iPod's ten. With it the
// limit is raised rather than removed: the score card keeps a hole's strokes in a signed byte
// (`SaveRecord::strokes_by_hole`), so a hole that ran past 127 would write a negative score.
[[nodiscard]] uint32_t stroke_limit();

// True when a rule-changing cheat has been on at any point during the round now in play.
[[nodiscard]] bool round_records_void();

// A new round begins: honest unless a rule-changing cheat is already on.
void round_records_reset();

// Read the flags from the platform's store, and write them back after a change. Both are
// no-ops on a platform whose store keeps nothing.
void load_cheats();
void save_cheats();

// The saved form, exposed for the tests.
[[nodiscard]] std::string cheats_to_text();
void cheats_from_text(const std::string& text);

}  // namespace minigolf::game
