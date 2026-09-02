// See host_text.h. The table of labels this port adds, and the two encodings a label can be
// asked for.
#include "host_text.h"

#include "cheats.h"
#include "game_state.h"
#include "runtime/memory.h"
#include "state.h"

#include <cstdio>

namespace minigolf::game {

namespace {

// A toggle row reads "NAME: ON". The buffer is written and immediately copied into guest memory
// by the one caller below, so one is enough; nothing holds a label across two loads.
constexpr unsigned LABEL_LIMIT = 32;

const char* toggle_label(const char* name, bool on) {
    static char label[LABEL_LIMIT];
    std::snprintf(label, sizeof label, "%s: %s", name, on ? "ON" : "OFF");
    return label;
}

const char* cheat_row_label(Cheat cheat) {
    return toggle_label(cheat_label(cheat), cheat_enabled(cheat));
}

// The label for one id. A row that shows a setting's value builds its line here rather than
// using the game's own heading-and-value rows, because those take both halves from the pack.
const char* label_of(HostText text) {
    switch (text) {
    case HostText::CheatsRow:
        return "CHEATS";
    case HostText::CheatsTitle:
        return "CHEATS";
    case HostText::UnlockCourses:
        return cheat_row_label(Cheat::UnlockCourses);
    case HostText::NoStrokeLimit:
        return cheat_row_label(Cheat::NoStrokeLimit);
    case HostText::NoOutOfBounds:
        return cheat_row_label(Cheat::NoOutOfBounds);
    case HostText::AimGuide:
        return cheat_row_label(Cheat::AimGuide);
    case HostText::GhostTrail:
        return cheat_row_label(Cheat::GhostTrail);
    }
    return "";
}

// Only the oracles set this; see `set_port_additions_hidden`.
bool additions_hidden;

}  // namespace

void set_port_additions_hidden(bool hidden) {
    additions_hidden = hidden;
}

bool port_additions_hidden() {
    return additions_hidden;
}

void host_text_write(uint32_t destination, const char* ascii) {
    const bool wide = static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
    uint32_t i = 0;
    for (; ascii[i] != '\0'; ++i) {
        const uint32_t character = static_cast<uint8_t>(ascii[i]);
        if (wide) {
            guest<uint16_t>(destination + i * 2) = static_cast<uint16_t>(character);
        } else {
            guest<uint8_t>(destination + i) = static_cast<uint8_t>(character);
        }
    }
    if (wide) {
        guest<uint16_t>(destination + i * 2) = static_cast<uint16_t>(0);
    } else {
        guest<uint8_t>(destination + i) = static_cast<uint8_t>(0);
    }
}

void host_text_load(uint32_t text_id, uint32_t destination) {
    host_text_write(destination, label_of(static_cast<HostText>(text_id & ~HOST_TEXT_FLAG)));
}

}  // namespace minigolf::game
