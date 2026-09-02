// See round_history.h.
#include "round_history.h"

#include "cheats.h"
#include "game_state.h"
#include "platform/save_store.h"
#include "state.h"

#include <sstream>

namespace minigolf::game {

namespace {

constexpr const char* HISTORY_NAME = "rounds.txt";

// Bumped whenever a line's meaning changes, so a file written against the old one is left alone
// rather than read as something it is not. The same rule the settings file follows.
constexpr unsigned HISTORY_FORMAT = 1;

// A hole is sampled every physics step, and a ball that is stepped a hundred times in one frame
// (the fast-forward state) fills a buffer quickly. This bounds what one hole can cost before
// the decimation on the way into the file ever runs.
constexpr size_t SAMPLES_MAX = 4096;

// The ball is bounded to a 0..255 course grid (`point_blocked`, physics.cpp), so a coordinate
// is one byte. A step smaller than this is not worth a point of its own.
constexpr int32_t SAMPLE_MIN_STEP = 1;

struct HoleRecord {
    uint32_t best = 0;  // strokes; 0 = never finished
    GhostPath ghost;
};

struct CourseRecord {
    HoleRecord holes[HOLES_PER_COURSE];
    uint32_t rounds = 0;
    uint32_t total_strokes = 0;  // of every finished round, for the average
};

CourseRecord courses[COURSE_COUNT];

// The hole being played, sampled but not yet judged against what is on file.
GhostPath recording;

// Set when the hole ran out its strokes; see `hole_abandoned`.
bool hole_was_abandoned;

const GhostPath& no_path() {
    static const GhostPath empty;
    return empty;
}

bool in_range(uint32_t course, uint32_t hole) {
    return course < COURSE_COUNT && hole < HOLES_PER_COURSE;
}

// A round that counts: single player or practice, honest, and not pass 'n play — where a hole's
// score is two players' and neither of them is "the" path.
bool round_is_recordable() {
    return static_cast<uint32_t>(menu_state().game_mode) != MODE_PASS_N_PLAY &&
           !round_records_void();
}

uint8_t clamp_coordinate(uint32_t fixed_16_16) {
    const int32_t whole = static_cast<int32_t>(fixed_16_16) >> 16;
    if (whole < 0) {
        return 0;
    }
    if (whole > 0xff) {
        return 0xff;
    }
    return static_cast<uint8_t>(whole);
}

// `source` reduced to at most GHOST_POINTS, keeping the first and last and spreading the rest
// evenly between them, so the shape of the path survives however long the recording was.
GhostPath decimated(const GhostPath& source) {
    if (source.size() <= GHOST_POINTS) {
        return source;
    }
    GhostPath out;
    out.x.reserve(GHOST_POINTS);
    out.y.reserve(GHOST_POINTS);
    const size_t last = source.size() - 1;
    for (unsigned i = 0; i < GHOST_POINTS; ++i) {
        const size_t at = i * last / (GHOST_POINTS - 1);
        out.x.push_back(source.x[at]);
        out.y.push_back(source.y[at]);
    }
    return out;
}

// A path as one hex string, two characters a coordinate, x before y.
std::string path_to_hex(const GhostPath& path) {
    static const char* DIGITS = "0123456789abcdef";
    std::string out;
    out.reserve(path.size() * 4);
    for (size_t i = 0; i < path.size(); ++i) {
        for (const uint8_t value : {path.x[i], path.y[i]}) {
            out.push_back(DIGITS[value >> 4]);
            out.push_back(DIGITS[value & 0xf]);
        }
    }
    return out;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// The inverse, ignoring anything that is not a whole point: a truncated or hand-mangled line
// costs its own ghost and nothing else.
GhostPath path_from_hex(const std::string& hex) {
    GhostPath path;
    for (size_t i = 0; i + 3 < hex.size(); i += 4) {
        const int x_high = hex_digit(hex[i]), x_low = hex_digit(hex[i + 1]);
        const int y_high = hex_digit(hex[i + 2]), y_low = hex_digit(hex[i + 3]);
        if (x_high < 0 || x_low < 0 || y_high < 0 || y_low < 0) {
            break;
        }
        path.x.push_back(static_cast<uint8_t>(x_high * 16 + x_low));
        path.y.push_back(static_cast<uint8_t>(y_high * 16 + y_low));
    }
    return path;
}

// The `format` line's number, or 0 for a file that has none.
unsigned format_of(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string name;
        unsigned version = 0;
        if ((fields >> name) && name == "format" && (fields >> version)) {
            return version;
        }
    }
    return 0;
}

}  // namespace

void round_begin() {
    recording = GhostPath();
    hole_was_abandoned = false;
}

void hole_begin() {
    recording = GhostPath();
    hole_was_abandoned = false;
}

void ball_sample() {
    if (!round_is_recordable() || recording.size() >= SAMPLES_MAX) {
        return;
    }
    const uint8_t x = clamp_coordinate(play_state().ball_x);
    const uint8_t y = clamp_coordinate(play_state().ball_y);
    if (!recording.empty()) {
        const int32_t dx = static_cast<int32_t>(x) - recording.x.back();
        const int32_t dy = static_cast<int32_t>(y) - recording.y.back();
        const int32_t moved = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (moved < SAMPLE_MIN_STEP) {
            return;  // the ball has not moved a whole course unit; the point would be a repeat
        }
    }
    recording.x.push_back(x);
    recording.y.push_back(y);
}

void hole_abandoned() {
    hole_was_abandoned = true;
}

void hole_finished(uint32_t course, uint32_t hole, uint32_t strokes) {
    if (!in_range(course, hole) || strokes == 0 || hole_was_abandoned || !round_is_recordable()) {
        return;
    }
    HoleRecord& record = courses[course].holes[hole];
    if (record.best != 0 && strokes >= record.best) {
        return;  // no better than what is on file: the ghost on file is still the one to beat
    }
    record.best = strokes;
    record.ghost = decimated(recording);
    save_round_history();
}

void round_finished(uint32_t course, uint32_t total) {
    if (course >= COURSE_COUNT || total == 0 || !round_is_recordable()) {
        return;
    }
    courses[course].rounds += 1;
    courses[course].total_strokes += total;
    save_round_history();
}

uint32_t hole_best(uint32_t course, uint32_t hole) {
    return in_range(course, hole) ? courses[course].holes[hole].best : 0;
}

const GhostPath& hole_ghost(uint32_t course, uint32_t hole) {
    return in_range(course, hole) ? courses[course].holes[hole].ghost : no_path();
}

uint32_t course_rounds(uint32_t course) {
    return course < COURSE_COUNT ? courses[course].rounds : 0;
}

uint32_t course_average_round(uint32_t course) {
    if (course >= COURSE_COUNT || courses[course].rounds == 0) {
        return 0;
    }
    const uint32_t rounds = courses[course].rounds;
    return (courses[course].total_strokes + rounds / 2) / rounds;  // to the nearest stroke
}

uint32_t holes_with_a_best() {
    uint32_t counted = 0;
    for (const CourseRecord& course : courses) {
        for (const HoleRecord& hole : course.holes) {
            counted += hole.best != 0 ? 1 : 0;
        }
    }
    return counted;
}

void round_history_clear() {
    for (CourseRecord& course : courses) {
        course = CourseRecord();
    }
    recording = GhostPath();
    hole_was_abandoned = false;
}

std::string round_history_to_text() {
    std::ostringstream out;
    out << "# Mini Golf: the best score on each hole, the path that scored it, and how the\n"
           "# rounds on each course have gone. This port's own; the game's save file\n"
           "# (jdmgp.sav) is untouched by it. Deleting this file forgets all of it.\n"
           "#   hole   <course> <hole> <strokes> <path, two hex digits per coordinate>\n"
           "#   course <course> <rounds> <total strokes>\n";
    out << "format " << HISTORY_FORMAT << '\n';
    for (uint32_t course = 0; course < COURSE_COUNT; ++course) {
        for (uint32_t hole = 0; hole < HOLES_PER_COURSE; ++hole) {
            const HoleRecord& record = courses[course].holes[hole];
            if (record.best == 0) {
                continue;
            }
            out << "hole " << course << ' ' << hole << ' ' << record.best << ' '
                << path_to_hex(record.ghost) << '\n';
        }
        if (courses[course].rounds != 0) {
            out << "course " << course << ' ' << courses[course].rounds << ' '
                << courses[course].total_strokes << '\n';
        }
    }
    return out.str();
}

void round_history_from_text(const std::string& text) {
    if (format_of(text) != HISTORY_FORMAT) {
        return;  // written by a version that meant something else by these; leave them alone
    }
    round_history_clear();
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string name;
        if (!(fields >> name) || name.empty() || name[0] == '#') {
            continue;
        }
        if (name == "hole") {
            uint32_t course = 0, hole = 0, strokes = 0;
            std::string hex;
            if (!(fields >> course >> hole >> strokes) || !in_range(course, hole) || strokes == 0) {
                continue;
            }
            fields >> hex;  // a hole with no path recorded is a line with nothing after the score
            HoleRecord& record = courses[course].holes[hole];
            record.best = strokes;
            record.ghost = path_from_hex(hex);
        } else if (name == "course") {
            uint32_t course = 0, rounds = 0, total = 0;
            if (!(fields >> course >> rounds >> total) || course >= COURSE_COUNT) {
                continue;
            }
            courses[course].rounds = rounds;
            courses[course].total_strokes = total;
        }
    }
}

void load_round_history() {
    std::vector<uint8_t> saved;
    if (!platform::save_store().load(HISTORY_NAME, saved) || saved.empty()) {
        return;  // nothing played yet
    }
    round_history_from_text(std::string(saved.begin(), saved.end()));
}

void save_round_history() {
    const std::string text = round_history_to_text();
    (void)platform::save_store().store(HISTORY_NAME,
                                       std::vector<uint8_t>(text.begin(), text.end()));
}

}  // namespace minigolf::game
