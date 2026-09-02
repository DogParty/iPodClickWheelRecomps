// See cheats.h.
#include "cheats.h"

#include "game_state.h"
#include "platform/save_store.h"
#include "state.h"

#include <sstream>
#include <vector>

namespace minigolf::game {

namespace {

constexpr const char* CHEATS_NAME = "cheats.txt";

// Bumped whenever a cheat's name or meaning changes, so a file written against the old meaning
// is left alone rather than quietly turning something on. The same rule the settings file
// follows (platform/settings.cpp).
constexpr unsigned CHEATS_FORMAT = 1;

// Raised, not removed: `SaveRecord::strokes_by_hole` is a signed byte a hole, so a limit past
// 127 would write a negative score onto the card. Ninety-nine also still fits the two digits
// the card draws.
constexpr uint32_t STROKE_LIMIT_NORMAL = 10, STROKE_LIMIT_RAISED = 99;

struct Description {
    Cheat cheat;
    const char* key;    // in the file
    const char* label;  // on the screen
    bool voids_records;
};

// The order the Cheats screen lists them in.
constexpr Description CHEATS[CHEAT_COUNT] = {
    {Cheat::UnlockCourses, "unlock-courses", "UNLOCK COURSES", true},
    {Cheat::NoStrokeLimit, "no-stroke-limit", "NO STROKE LIMIT", true},
    {Cheat::NoOutOfBounds, "no-out-of-bounds", "NO OUT OF BOUNDS", true},
    {Cheat::AimGuide, "aim-guide", "AIM GUIDE", true},
    {Cheat::GhostTrail, "ghost-trail", "GHOST TRAIL", false},
};

bool enabled[CHEAT_COUNT];

// Sticky for the length of a round: see cheats.h on why it cannot simply be recomputed.
bool records_void_this_round;

const Description& description(Cheat cheat) {
    return CHEATS[static_cast<uint32_t>(cheat)];
}

bool& flag(Cheat cheat) {
    return enabled[static_cast<uint32_t>(cheat)];
}

// True when any cheat that changes the rules is on right now.
bool any_voiding_cheat_on() {
    for (const Description& entry : CHEATS) {
        if (entry.voids_records && flag(entry.cheat)) {
            return true;
        }
    }
    return false;
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

const Cheat* all_cheats() {
    static Cheat order[CHEAT_COUNT];
    for (unsigned i = 0; i < CHEAT_COUNT; ++i) {
        order[i] = CHEATS[i].cheat;
    }
    return order;
}

const char* cheat_label(Cheat cheat) {
    return description(cheat).label;
}

const char* cheat_key(Cheat cheat) {
    return description(cheat).key;
}

bool cheat_voids_records(Cheat cheat) {
    return description(cheat).voids_records;
}

bool cheat_enabled(Cheat cheat) {
    return flag(cheat);
}

void cheat_set(Cheat cheat, bool value) {
    flag(cheat) = value;
    if (value && cheat_voids_records(cheat)) {
        records_void_this_round = true;
    }
    save_cheats();
}

void cheat_toggle(Cheat cheat) {
    cheat_set(cheat, !cheat_enabled(cheat));
}

uint32_t courses_available() {
    if (cheat_enabled(Cheat::UnlockCourses)) {
        return COURSE_COUNT;
    }
    return static_cast<uint32_t>(static_cast<int32_t>(screen_state().courses_unlocked));
}

uint32_t stroke_limit() {
    return cheat_enabled(Cheat::NoStrokeLimit) ? STROKE_LIMIT_RAISED : STROKE_LIMIT_NORMAL;
}

bool round_records_void() {
    return records_void_this_round;
}

void round_records_reset() {
    records_void_this_round = any_voiding_cheat_on();
}

std::string cheats_to_text() {
    std::ostringstream out;
    out << "# Mini Golf cheats. These are the port's, not the iPod game's; a round played with\n"
           "# any of them but ghost-trail does not count towards the best rounds or the\n"
           "# statistics. Deleting this file turns them all off.\n";
    out << "format " << CHEATS_FORMAT << '\n';
    for (const Description& entry : CHEATS) {
        out << entry.key << ' ' << (flag(entry.cheat) ? 1 : 0) << '\n';
    }
    return out.str();
}

void cheats_from_text(const std::string& text) {
    if (format_of(text) != CHEATS_FORMAT) {
        return;  // written by a version that meant something else by these; leave them alone
    }
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string name, value;
        if (!(fields >> name) || name.empty() || name[0] == '#' || !(fields >> value)) {
            continue;  // a comment, a blank line, or something this build does not understand
        }
        for (const Description& entry : CHEATS) {
            if (name == entry.key) {
                flag(entry.cheat) = value != "0";
            }
        }
    }
}

void load_cheats() {
    std::vector<uint8_t> saved;
    if (!platform::save_store().load(CHEATS_NAME, saved) || saved.empty()) {
        return;  // none saved yet: every cheat stays off
    }
    cheats_from_text(std::string(saved.begin(), saved.end()));
    records_void_this_round = any_voiding_cheat_on();
}

void save_cheats() {
    const std::string text = cheats_to_text();
    (void)platform::save_store().store(CHEATS_NAME, std::vector<uint8_t>(text.begin(), text.end()));
}

}  // namespace minigolf::game
