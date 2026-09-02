// The bindings table and where it is kept. See input_bindings.h.
#include "platform/input_bindings.h"

#include "platform/save_store.h"

#include <cstdlib>
#include <sstream>
#include <vector>

namespace minigolf::platform {

namespace {

// The bindings live beside the saved games, in the same platform store: both are the player's
// own data and both should follow them wherever that platform keeps such things.
constexpr const char* BINDINGS_NAME = "bindings.txt";

struct ActionNames {
    Action action;
    const char* key;
    const char* label;
};

// The order here is the order a settings window lists them: the wheel first, then the button in
// the middle of it, then the four around the outside as they sit on the device.
constexpr ActionNames NAMES[ACTION_COUNT] = {
    // The file names have to stay as they are: they are what a player's saved bindings say.
    {Action::SwipeLeft, "swipe-left", "Scroll left"},
    {Action::SwipeRight, "swipe-right", "Scroll right"},
    {Action::Select, "select", "Select"},
    {Action::PlayPause, "play-pause", "Play/Pause"},
    {Action::Menu, "menu", "Menu"},
    {Action::Rewind, "rewind", "Rewind"},
    {Action::FastForward, "fast-forward", "Fast forward"},
};

unsigned index_of(Action action) {
    return static_cast<unsigned>(action);
}

const InputChoice* choices = nullptr;
unsigned choice_count = 0;

}  // namespace

const Action* all_actions() {
    static Action order[ACTION_COUNT];
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        order[i] = NAMES[i].action;
    }
    return order;
}

const char* action_key(Action action) {
    return NAMES[index_of(action)].key;
}

const char* action_label(Action action) {
    return NAMES[index_of(action)].label;
}

void InputBindings::bind(Action action, InputCode code, unsigned slot) {
    if (slot >= BINDING_SLOTS) {
        return;
    }
    if (code != NO_INPUT) {
        for (unsigned i = 0; i < ACTION_COUNT; ++i) {
            for (unsigned s = 0; s < BINDING_SLOTS; ++s) {
                if (codes_[i][s] == code) {
                    codes_[i][s] = NO_INPUT;
                }
            }
        }
    }
    codes_[index_of(action)][slot] = code;
}

void InputBindings::clear(Action action, unsigned slot) {
    if (slot < BINDING_SLOTS) {
        codes_[index_of(action)][slot] = NO_INPUT;
    }
}

InputCode InputBindings::code(Action action, unsigned slot) const {
    return slot < BINDING_SLOTS ? codes_[index_of(action)][slot] : NO_INPUT;
}

bool InputBindings::action_for(InputCode code, Action& action) const {
    if (code == NO_INPUT) {
        return false;
    }
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        for (unsigned s = 0; s < BINDING_SLOTS; ++s) {
            if (codes_[i][s] == code) {
                action = NAMES[i].action;
                return true;
            }
        }
    }
    return false;
}

void InputBindings::restore_defaults() {
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        for (unsigned s = 0; s < BINDING_SLOTS; ++s) {
            codes_[i][s] = defaults_[i][s];
        }
    }
}

namespace {

// The format the bindings file is written in, bumped whenever a default changes; see from_text.
// 4 added a third slot per action, which the desktop's gamepad defaults fill.
constexpr unsigned BINDINGS_FORMAT = 4;

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

std::string InputBindings::to_text() const {
    std::ostringstream out;
    out << "# Mini Golf input bindings: one action per line, then the platform's codes for the\n"
           "# inputs that perform it. A code of 0 means deliberately unbound.\n";
    out << "format " << BINDINGS_FORMAT << '\n';
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        out << NAMES[i].key;
        for (unsigned s = 0; s < BINDING_SLOTS; ++s) {
            out << ' ' << codes_[i][s];
        }
        out << '\n';
    }
    return out.str();
}

void InputBindings::from_text(const std::string& text) {
    // Start from the platform's defaults, not from nothing: an action this build has and the
    // file does not should behave as it always has, rather than quietly doing nothing.
    restore_defaults();
    // A file written against a different set of defaults is not read. A default that moves — as
    // the letter keys did once typing a name became possible — would otherwise live on in every
    // file already saved, and the player would have to find Restore Defaults to be rid of it.
    if (format_of(text) != BINDINGS_FORMAT) {
        return;
    }
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string name;
        unsigned long long code = 0;
        if (!(fields >> name) || name.empty() || name[0] == '#' || !(fields >> code)) {
            continue;  // a comment, a blank line, or something this build does not understand
        }
        for (unsigned i = 0; i < ACTION_COUNT; ++i) {
            if (name != NAMES[i].key) {
                continue;
            }
            for (unsigned s = 0; s < BINDING_SLOTS; ++s) {
                if (code == NO_INPUT) {
                    clear(NAMES[i].action, s);  // written deliberately unbound
                } else {
                    bind(NAMES[i].action, static_cast<InputCode>(code), s);
                }
                code = 0;
                if (!(fields >> code)) {
                    code = NO_INPUT;  // a file from a build with fewer slots than this one
                }
            }
            break;
        }
    }
}

void set_assignable_inputs(const InputChoice* list, unsigned count) {
    choices = list;
    choice_count = count;
}

const InputChoice* assignable_inputs(unsigned& count) {
    count = choice_count;
    return choices;
}

const char* input_label(InputCode code) {
    if (code == NO_INPUT) {
        return "—";
    }
    for (unsigned i = 0; i < choice_count; ++i) {
        if (choices[i].code == code) {
            return choices[i].label;
        }
    }
    return "(unknown)";
}

InputBindings& input_bindings() {
    static InputBindings bindings;
    return bindings;
}

void set_default_bindings(const InputCode (&codes)[ACTION_COUNT][BINDING_SLOTS]) {
    InputBindings& bindings = input_bindings();
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        for (unsigned s = 0; s < BINDING_SLOTS; ++s) {
            bindings.defaults_[i][s] = codes[i][s];
        }
    }
    bindings.restore_defaults();
}

void load_input_bindings() {
    std::vector<uint8_t> saved;
    if (!save_store().load(BINDINGS_NAME, saved) || saved.empty()) {
        return;  // never rebound; the platform's defaults stand
    }
    input_bindings().from_text(std::string(saved.begin(), saved.end()));
}

void save_input_bindings() {
    const std::string text = input_bindings().to_text();
    (void)save_store().store(BINDINGS_NAME, std::vector<uint8_t>(text.begin(), text.end()));
}

}  // namespace minigolf::platform
