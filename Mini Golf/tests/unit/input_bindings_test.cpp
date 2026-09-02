// The input bindings (src/platform/input_bindings.{h,cpp}): which input does what.
//
// The table is the portable half of rebinding — every platform shares the actions, the "one
// input does one thing" rule, the defaults and the saved file, and supplies only the codes and
// a way to ask the player for one. These tests pin that half, so a second platform's settings
// window has something to build against.
#include "platform/input_bindings.h"
#include "platform/save_store.h"

#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

namespace {

using namespace minigolf::platform;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Stand-ins for a platform's key codes; only the platform knows what they mean.
constexpr InputCode UP = 100, DOWN = 101, SPACE = 102, W = 103, A = 104, S = 105, D = 106;
constexpr InputCode Z = 200, COMMA = 201, PERIOD = 202;

void install_defaults() {
    const InputCode defaults[ACTION_COUNT][BINDING_SLOTS] = {
        {UP, COMMA},   {DOWN, PERIOD}, {SPACE, NO_INPUT}, {S, NO_INPUT},
        {W, NO_INPUT}, {A, NO_INPUT},  {D, NO_INPUT},
    };
    set_default_bindings(defaults);
}

void test_actions() {
    // Every action is listed exactly once, and every one has both names.
    std::set<std::string> keys;
    std::set<std::string> labels;
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        const Action action = all_actions()[i];
        keys.insert(action_key(action));
        labels.insert(action_label(action));
    }
    check(keys.size() == ACTION_COUNT, "every action has its own name for the file");
    check(labels.size() == ACTION_COUNT, "every action has its own name for a person");
    check(std::string(action_label(Action::SwipeLeft)) == "Scroll left", "labels read as words");
    check(std::string(action_key(Action::FastForward)) == "fast-forward", "keys are stable");
}

void test_binding() {
    install_defaults();
    InputBindings& bindings = input_bindings();
    check(bindings.code(Action::Select) == SPACE, "the platform's defaults are installed");

    Action found = Action::Select;
    check(bindings.action_for(UP, found) && found == Action::SwipeLeft,
          "an input finds its action");
    check(!bindings.action_for(Z, found), "an unbound input finds nothing");

    bindings.bind(Action::Menu, Z);
    check(bindings.code(Action::Menu) == Z, "an action can be rebound");
    check(bindings.action_for(Z, found) && found == Action::Menu, "and answers to its new input");
    check(!bindings.action_for(W, found), "the input it used to have is now unbound");

    // The second slot: another input for the same action, found the same way.
    check(bindings.code(Action::SwipeLeft, 1) == COMMA, "an action can have a second input");
    check(bindings.action_for(COMMA, found) && found == Action::SwipeLeft,
          "which performs the same action");
    bindings.bind(Action::SwipeRight, COMMA, 1);
    check(bindings.code(Action::SwipeLeft, 1) == NO_INPUT,
          "one input does one thing, across slots as well");
    check(bindings.code(Action::SwipeRight, 1) == COMMA, "and the new owner has it");
    check(bindings.code(Action::SwipeRight) == DOWN, "leaving its first input alone");

    // One input does one thing: giving Select the key Menu already has takes it away from Menu.
    bindings.bind(Action::Select, Z);
    check(bindings.code(Action::Select) == Z, "the new owner has it");
    check(bindings.code(Action::Menu) == NO_INPUT, "and the previous owner has nothing");
    check(bindings.action_for(Z, found) && found == Action::Select, "one input, one action");

    bindings.clear(Action::Select);
    check(bindings.code(Action::Select) == NO_INPUT, "a binding can be removed");
    check(!bindings.action_for(Z, found), "and then does nothing");

    bindings.restore_defaults();
    check(bindings.code(Action::Select) == SPACE && bindings.code(Action::Menu) == W,
          "restoring defaults puts back what the platform asked for");
}

void test_text() {
    install_defaults();
    InputBindings& bindings = input_bindings();
    bindings.bind(Action::Menu, Z);
    const std::string saved = bindings.to_text();

    bindings.restore_defaults();
    bindings.from_text(saved);
    check(bindings.code(Action::Menu) == Z, "what was saved is what is read back");
    check(bindings.code(Action::SwipeLeft) == UP, "and so is everything else");

    // An action deliberately left unbound is written as such, and comes back unbound.
    bindings.clear(Action::Rewind);
    bindings.from_text(bindings.to_text());
    check(bindings.code(Action::Rewind) == NO_INPUT, "an unbound action stays unbound");

    // A file from another build: unknown actions and rubbish lines are skipped, the rest loads.
    // The format number here has to be the current one — a file of any other format is not read
    // at all, which is the rule the end of this function checks.
    bindings.from_text("# a comment\nformat 4\nselect 42\nsomething-else 7\nswipe-left 43 44\n");
    check(bindings.code(Action::Select) == 42, "a known action loads");
    check(bindings.code(Action::SwipeLeft) == 43, "even after a line it cannot use");
    check(bindings.code(Action::SwipeLeft, 1) == 44, "with both of the inputs it names");
    check(bindings.code(Action::Select, 1) == NO_INPUT, "a line with one input leaves no second");
    // A build with more slots than the file has: the slots the file does not reach are unbound
    // rather than left holding a default the file meant to replace.
    check(bindings.code(Action::SwipeLeft, 2) == NO_INPUT, "nor a third");

    // An action the file never mentions keeps the platform's default rather than going quiet —
    // otherwise adding an action would leave everyone who had ever saved bindings without it.
    check(bindings.code(Action::FastForward) == D, "an action the file omits keeps its default");

    // A file written against a different set of defaults is not read at all: it would otherwise
    // keep handing back a default that has since moved.
    bindings.from_text("format 2\nselect 42\n");
    check(bindings.code(Action::Select) == SPACE, "a file of another format leaves the defaults");
    bindings.from_text("select 42\n");
    check(bindings.code(Action::Select) == SPACE, "and so does one with no format at all");
}

// The wiring a settings window depends on: a change is written to the platform's store and is
// there the next time the game starts.
void test_persistence() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "minigolf-bindings-test";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    set_save_store(make_directory_save_store(directory.string()));

    install_defaults();
    load_input_bindings();  // nothing saved yet
    check(input_bindings().code(Action::Menu) == W, "with nothing saved, the defaults stand");

    input_bindings().bind(Action::Menu, Z);
    save_input_bindings();

    // A fresh start: the platform installs its defaults, then the player's bindings load over.
    install_defaults();
    check(input_bindings().code(Action::Menu) == W, "a fresh start begins at the defaults");
    load_input_bindings();
    check(input_bindings().code(Action::Menu) == Z, "and the saved binding is read back");
    check(input_bindings().code(Action::Select) == SPACE, "along with the ones never changed");

    std::filesystem::remove_all(directory, error);
}

}  // namespace

int main() {
    test_actions();
    test_binding();
    test_text();
    test_persistence();
    if (failures != 0) {
        std::fprintf(stderr, "input bindings: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("input bindings: ok");
    return 0;
}
