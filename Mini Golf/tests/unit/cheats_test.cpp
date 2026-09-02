// The cheats (src/game/cheats.{h,cpp}): their saved form, and the rule that decides whether a
// round counts.
//
// The screen that shows them needs a game running and is exercised by a scripted session; what
// is pinned here is the part every screen relies on — that a flag survives a round trip, that a
// file written against another meaning is not read, and above all that turning a rule-changing
// cheat on voids the round and that turning it off again does not un-void it.
#include "game/cheats.h"

#include <cstdio>
#include <string>

namespace {

using namespace minigolf::game;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void all_off() {
    for (unsigned i = 0; i < CHEAT_COUNT; ++i) {
        cheat_set(all_cheats()[i], false);
    }
    round_records_reset();
}

void test_defaults() {
    all_off();
    for (unsigned i = 0; i < CHEAT_COUNT; ++i) {
        check(!cheat_enabled(all_cheats()[i]), "every cheat starts off");
    }
    check(!round_records_void(), "and a round with none of them on counts");
}

// The one cheat that is not a cheat: it shows the player their own past path and changes no
// rule, so a round played with it still counts. The rest all void.
void test_which_cheats_void_records() {
    check(cheat_voids_records(Cheat::UnlockCourses), "unlocking the courses voids a round");
    check(cheat_voids_records(Cheat::NoStrokeLimit), "so does lifting the stroke limit");
    check(cheat_voids_records(Cheat::NoOutOfBounds), "so does keeping the ball in bounds");
    check(cheat_voids_records(Cheat::AimGuide), "so does the aim guide");
    check(!cheat_voids_records(Cheat::GhostTrail), "the ghost trail does not");
}

void test_voiding_is_sticky() {
    all_off();
    cheat_set(Cheat::GhostTrail, true);
    check(!round_records_void(), "the ghost trail leaves the round countable");

    cheat_set(Cheat::NoStrokeLimit, true);
    check(round_records_void(), "a rule-changing cheat voids the round at once");

    // The point of the whole arrangement: it cannot be switched on for one awkward hole and
    // switched off again before the card is written.
    cheat_set(Cheat::NoStrokeLimit, false);
    check(round_records_void(), "and turning it off again does not un-void the round");

    round_records_reset();
    check(!round_records_void(), "only a new round starts honest again");

    // A round that begins with one already on is void from its first stroke.
    cheat_set(Cheat::AimGuide, true);
    round_records_reset();
    check(round_records_void(), "a round begun with one on is void from the start");
    all_off();
}

void test_stroke_limit() {
    all_off();
    check(stroke_limit() == 10, "the iPod's ten strokes");
    cheat_set(Cheat::NoStrokeLimit, true);
    const uint32_t raised = stroke_limit();
    check(raised > 10, "the cheat raises it");
    // Raised, not removed: a hole's score goes onto the card in a signed byte.
    check(raised <= 127, "but not past what a signed byte on the score card can hold");
    all_off();
}

void test_text_round_trip() {
    all_off();
    cheat_set(Cheat::UnlockCourses, true);
    cheat_set(Cheat::GhostTrail, true);
    const std::string written = cheats_to_text();

    all_off();
    cheats_from_text(written);
    check(cheat_enabled(Cheat::UnlockCourses), "a cheat that was on is on again");
    check(cheat_enabled(Cheat::GhostTrail), "and so is the other one");
    check(!cheat_enabled(Cheat::AimGuide), "one that was off stays off");

    // A file written against another meaning of these is left alone entirely, rather than
    // turning something on that the name no longer means.
    all_off();
    cheats_from_text("format 99\nunlock-courses 1\n");
    check(!cheat_enabled(Cheat::UnlockCourses), "a file of another format changes nothing");
    cheats_from_text("unlock-courses 1\n");
    check(!cheat_enabled(Cheat::UnlockCourses), "nor does one with no format line");

    // A name this build does not know sits beside one it does without spoiling it.
    all_off();
    cheats_from_text("format 1\naim-guide 1\nsome-later-cheat 1\n");
    check(cheat_enabled(Cheat::AimGuide), "a known cheat loads past one that is not");
    all_off();
}

void test_names_are_distinct() {
    for (unsigned i = 0; i < CHEAT_COUNT; ++i) {
        for (unsigned j = i + 1; j < CHEAT_COUNT; ++j) {
            check(std::string(cheat_key(all_cheats()[i])) != cheat_key(all_cheats()[j]),
                  "no two cheats share a name in the file");
            check(std::string(cheat_label(all_cheats()[i])) != cheat_label(all_cheats()[j]),
                  "and none share a label on the screen");
        }
    }
}

}  // namespace

int main() {
    test_defaults();
    test_which_cheats_void_records();
    test_voiding_is_sticky();
    test_stroke_limit();
    test_text_round_trip();
    test_names_are_distinct();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("cheats: all checks passed\n");
    return 0;
}
