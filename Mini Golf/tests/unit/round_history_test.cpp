// The round history's saved form (src/game/round_history.{h,cpp}): the best score on each hole,
// the path that scored it, and how the rounds on each course have gone.
//
// The recording half needs a game running and is exercised by playing one (tests/scripts); what
// is pinned here is everything underneath it — that a file survives a round trip, that a path is
// decimated rather than truncated, and that a file written against another meaning is not read,
// which is the rule every one of this port's files follows.
#include "game/round_history.h"

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

// A file naming one hole, with `points` points on its path, as `round_history_to_text` writes it.
std::string file_with_path(unsigned points) {
    std::string text = "format 1\nhole 1 5 3 ";
    for (unsigned i = 0; i < points; ++i) {
        static const char* digits = "0123456789abcdef";
        const unsigned x = i & 0xff, y = (i * 3) & 0xff;
        text += digits[x >> 4];
        text += digits[x & 0xf];
        text += digits[y >> 4];
        text += digits[y & 0xf];
    }
    return text + "\ncourse 1 4 240\n";
}

void test_empty() {
    round_history_clear();
    check(hole_best(0, 0) == 0, "a hole never finished has no best");
    check(hole_ghost(0, 0).empty(), "and no path");
    check(holes_with_a_best() == 0, "nothing is on file");
    check(course_rounds(0) == 0, "no rounds on any course");
    check(course_average_round(0) == 0, "and so no average to report");
}

void test_round_trip() {
    round_history_clear();
    round_history_from_text(file_with_path(10));
    check(hole_best(1, 5) == 3, "the hole's best is read back");
    check(hole_ghost(1, 5).size() == 10, "with every point of its path");
    check(hole_ghost(1, 5).x[2] == 2 && hole_ghost(1, 5).y[2] == 6, "and the points themselves");
    check(holes_with_a_best() == 1, "one hole is on file");
    check(course_rounds(1) == 4, "the course's rounds are read back");
    check(course_average_round(1) == 60, "240 strokes over 4 rounds averages 60");

    // Written out and read back in again says the same thing.
    const std::string written = round_history_to_text();
    round_history_clear();
    round_history_from_text(written);
    check(hole_best(1, 5) == 3, "a best survives being written and read");
    check(hole_ghost(1, 5).size() == 10, "so does its path");
    check(course_average_round(1) == 60, "so does the average");
}

void test_average_rounds_to_nearest() {
    round_history_clear();
    round_history_from_text("format 1\ncourse 0 3 100\n");  // 33.33
    check(course_average_round(0) == 33, "an average rounds down when it should");
    round_history_clear();
    round_history_from_text("format 1\ncourse 0 3 101\n");  // 33.67
    check(course_average_round(0) == 34, "and up when it should");
}

// A path longer than the cap is thinned out, not cut off: the last point of the recording is
// still the last point on file, so the ghost still reaches the cup.
void test_long_path_is_decimated() {
    round_history_clear();
    round_history_from_text(file_with_path(GHOST_POINTS * 4));
    check(hole_ghost(1, 5).size() == GHOST_POINTS * 4,
          "a file may hold as many points as it likes; only recording decimates");

    round_history_clear();
    round_history_from_text(file_with_path(10));
    const GhostPath& path = hole_ghost(1, 5);
    check(path.x.front() == 0, "the path starts where the recording did");
    check(path.x.back() == 9, "and ends where it ended");
}

void test_other_formats_and_junk() {
    round_history_clear();
    round_history_from_text(file_with_path(4));
    round_history_from_text("format 99\nhole 0 0 1 \n");
    check(hole_best(1, 5) == 3, "a file of another format changes nothing");
    check(hole_best(0, 0) == 0, "not even the lines it names");

    round_history_clear();
    round_history_from_text("format 1\nhole 9 99 3 0000\nhole 0 0 0 0000\nhole 0 1 2\n"
                            "course 7 1 60\nsomething else entirely\n");
    check(hole_best(0, 1) == 2, "a hole with no path recorded still keeps its score");
    check(hole_ghost(0, 1).empty(), "and has no path");
    check(holes_with_a_best() == 1, "an out-of-range hole, and a score of zero, are ignored");
    check(course_rounds(0) == 0, "as is an out-of-range course");
}

// A hole's line can be truncated by a half-written file; it should cost that ghost and nothing
// else, rather than reading past the end of the string.
void test_truncated_path() {
    round_history_clear();
    round_history_from_text("format 1\nhole 0 0 4 0102030\n");
    check(hole_best(0, 0) == 4, "the score is still read");
    check(hole_ghost(0, 0).size() == 1, "and the whole points before the truncation");

    round_history_clear();
    round_history_from_text("format 1\nhole 0 0 4 zzzz\n");
    check(hole_ghost(0, 0).empty(), "a path that is not hex at all is simply no path");
}

}  // namespace

int main() {
    test_empty();
    test_round_trip();
    test_average_rounds_to_nearest();
    test_long_path_is_decimated();
    test_other_formats_and_junk();
    test_truncated_path();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("round history: all checks passed\n");
    return 0;
}
