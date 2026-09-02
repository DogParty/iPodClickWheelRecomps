// See switch_settings.h.
#include "platform/switch/switch_settings.h"

#include "platform/input_bindings.h"

#include <cstdio>

namespace minigolf::platform {

namespace {

// The rows below the seven actions.
constexpr unsigned ROW_DEFAULTS = ACTION_COUNT, ROW_BACK = ACTION_COUNT + 1;
constexpr unsigned ROW_COUNT = ACTION_COUNT + 2;

// Plus leaves the game and Minus opens this screen, so neither can be bound to anything; the
// screen would otherwise be a place a player could shut the door of behind them.
constexpr uint64_t RESERVED = HidNpadButton_Plus | HidNpadButton_Minus;

void draw(unsigned row, unsigned slot, bool waiting) {
    consoleClear();
    std::printf("\x1b[1;2HControls\n\n");
    const Action* order = all_actions();
    for (unsigned i = 0; i < ACTION_COUNT; ++i) {
        std::printf("  %c %-14s", row == i ? '>' : ' ', action_label(order[i]));
        for (unsigned s = 0; s < BINDING_SLOTS; ++s) {
            const bool here = row == i && slot == s;
            const char* name = input_label(input_bindings().code(order[i], s));
            std::printf("%s%-18s%s", here ? "[" : " ", waiting && here ? "press a button" : name,
                        here ? "]" : " ");
        }
        std::printf("\n");
    }
    std::printf("\n  %c Restore defaults\n", row == ROW_DEFAULTS ? '>' : ' ');
    std::printf("  %c Back to the game\n", row == ROW_BACK ? '>' : ' ');
    std::printf("\n\n  Up and Down choose a control, Left and Right its first or second button.\n"
                "  A binds the button you press next. Y clears it. B goes back.\n"
                "\n  A button does one thing: binding one takes it from wherever it was.\n");
    consoleUpdate(nullptr);
}

}  // namespace

void switch_settings_screen(PadState& pad) {
    unsigned row = 0, slot = 0;
    bool waiting = false;  // waiting for the player to press the button they want bound
    draw(row, slot, waiting);
    while (appletMainLoop()) {
        padUpdate(&pad);
        const uint64_t down = padGetButtonsDown(&pad);
        if (down == 0) {
            consoleUpdate(nullptr);
            continue;
        }

        if (waiting) {
            // Any button but the two the program keeps for itself. Two buttons pressed in the
            // same frame are one binding each in the making, not a chord: the lowest bit is
            // taken, since a binding is one button and `action_for` compares the whole code.
            const uint64_t wanted = down & ~RESERVED;
            if (wanted != 0) {
                input_bindings().bind(all_actions()[row],
                                      static_cast<InputCode>(wanted & (~wanted + 1)), slot);
                save_input_bindings();
            }
            waiting = false;
            draw(row, slot, waiting);
            continue;
        }

        if ((down & (HidNpadButton_AnyUp)) != 0) {
            row = row == 0 ? ROW_COUNT - 1 : row - 1;
        } else if ((down & (HidNpadButton_AnyDown)) != 0) {
            row = row + 1 == ROW_COUNT ? 0 : row + 1;
        } else if ((down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) != 0) {
            slot = (slot + 1) % BINDING_SLOTS;
        } else if ((down & HidNpadButton_A) != 0) {
            if (row == ROW_BACK) {
                return;
            }
            if (row == ROW_DEFAULTS) {
                input_bindings().restore_defaults();
                save_input_bindings();
            } else {
                waiting = true;
            }
        } else if ((down & HidNpadButton_Y) != 0 && row < ACTION_COUNT) {
            input_bindings().clear(all_actions()[row], slot);
            save_input_bindings();
        } else if ((down & (HidNpadButton_B | HidNpadButton_Minus | HidNpadButton_Plus)) != 0) {
            return;
        }
        draw(row, slot, waiting);
    }
}

}  // namespace minigolf::platform
