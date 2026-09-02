// What this port remembers about rounds already played, which the iPod game had nowhere to put.
//
// The 2008 save record keeps three numbers and one best round a course (records.h,
// `SaveRecord`): 0x144 bytes with nothing spare in them, written byte for byte the way the iPod
// wrote them so that a save still means the same thing to the original game. So nothing here
// goes in it. This is a second file in the same store the settings and the key bindings already
// use (platform/save_store.h), holding what the record has no room for:
//
//  * the best score on each of the 54 holes, which the original never kept — it knew only your
//    best *round* on each course, so a good hole in a bad round left no trace;
//  * how many rounds each course has seen, and their total, which is what an average is made of;
//  * the path the ball took on the round that scored each hole's best, which is what the ghost
//    trail draws (hole_overlays.cpp).
//
// A path is kept in course coordinates — the same 0..255 grid `point_blocked` (physics.cpp)
// bounds the ball to — so a point is two bytes, and a hole's path is decimated to at most
// GHOST_POINTS on the way into the file. Fifty-four of those is a few kilobytes, which is the
// most this file will ever cost.
//
// Nothing is recorded for a round whose records are void (cheats.h) or for pass 'n play, where
// "the" score on a hole is two players' and neither is the ghost.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace minigolf::game {

// The most points a stored path keeps. A hole's recording is decimated to this on the way in;
// 128 is comfortably more than a hole's worth of ball at the resolution it is sampled.
constexpr unsigned GHOST_POINTS = 128;

// A ball's path, as course coordinates. Both arrays are the same length.
struct GhostPath {
    std::vector<uint8_t> x, y;

    [[nodiscard]] bool empty() const { return x.empty(); }
    [[nodiscard]] size_t size() const { return x.size(); }
};

// --- recording the round in play ---------------------------------------------------------------

// A new round: whatever was being recorded is dropped.
void round_begin();

// A new hole: the path starts again from the tee.
void hole_begin();

// One physics step has been taken; note where the ball is now. Called from the two places that
// step the ball (hole_tick.cpp), so the path is sampled at the rate the ball actually moves
// rather than once a frame.
void ball_sample();

// The hole ended at the stroke limit rather than in the cup. Nothing about it is remembered:
// the limit is not a score anyone set, and its path is not a line worth following. Told
// separately because the game itself cannot be asked afterwards — it writes the limit onto the
// score card exactly as it writes a hole holed out on the last stroke.
void hole_abandoned();

// A hole is over in `strokes`. A first finish, or a better one than is on file, replaces the
// hole's best and keeps the path that scored it.
//
// Only a hole holed out gets here in single player: one that runs out its strokes reaches the
// score card by another route (`ball_rest_record` opens the card the moment a result of "holed"
// comes to rest), which never passes through the tick this is called from. In Practice Hole it
// does pass through, which is why `hole_abandoned` exists rather than the two modes quietly
// recording different things.
void hole_finished(uint32_t course, uint32_t hole, uint32_t strokes);

// All eighteen holes of `course` are over in `total` strokes.
void round_finished(uint32_t course, uint32_t total);

// --- what has been remembered ------------------------------------------------------------------

// The best score on a hole, or 0 when it has never been finished.
[[nodiscard]] uint32_t hole_best(uint32_t course, uint32_t hole);

// The path that scored it; empty when there is none.
[[nodiscard]] const GhostPath& hole_ghost(uint32_t course, uint32_t hole);

[[nodiscard]] uint32_t course_rounds(uint32_t course);

// The mean round on a course, rounded to the nearest stroke; 0 when none has been finished.
[[nodiscard]] uint32_t course_average_round(uint32_t course);

// How many of the 54 holes have a best score on file.
[[nodiscard]] uint32_t holes_with_a_best();

// --- the file ----------------------------------------------------------------------------------

void load_round_history();
void save_round_history();

// The saved form, exposed for the tests.
[[nodiscard]] std::string round_history_to_text();
void round_history_from_text(const std::string& text);

// Forget everything. What Reset Game does to the save record, done to this file too.
void round_history_clear();

}  // namespace minigolf::game
