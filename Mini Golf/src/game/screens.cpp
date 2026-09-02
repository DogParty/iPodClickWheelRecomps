// The screen object, the two tick wrappers the flow calls, and the menu sounds.
//
// Every screen is run through one of two wrappers the flow selects by screen id: the title
// wrapper (0x18006cb4) just runs the screen's tick; the menu wrapper (0x18006688) runs it and
// then, once the menu has finished sliding in, handles the selection: the automatic
// demonstration on the first two screens, Select on item 5, Menu on item 6.
#include "screens.h"

#include "calling.h"
#include "framework/device.h"
#include "game_state.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"

namespace minigolf::game {

void sound_effect_play(uint32_t slot, uint32_t index, uint32_t volume, uint32_t rate, uint32_t pan);

namespace {

// 0x18011400, the voice every sound plays on: the hook only ever answered this.
constexpr uint32_t VOICE = 0x7fff;

constexpr uint32_t SCREEN_COUNT = 15;
constexpr uint32_t MENU_SOUND_COUNT = 10;
constexpr uint32_t EVENT_SELECT = 5, EVENT_MENU = 6;
constexpr uint32_t PHASE_STEADY = 2;

// Still recompiled, named by their use here (inferred).

}  // namespace

// screen_set reached by a tail call: the same, but the caller's return address is what the
// original's branch carried (it has no calls of its own, so only the name differs).
void screen_set_tail(uint32_t id) {
    screen_set(id);
}

void screen_set(uint32_t id) {
    if (id >= SCREEN_COUNT) {
        assert_trap(0x18004070u);
    }
    screen_state().previous_id = static_cast<uint8_t>(static_cast<uint32_t>(screen_state().id));
    screen_state().id = static_cast<uint8_t>(id);
    text_block().frame_count = 0;
}

void tick_nothing(uint32_t /*milliseconds*/) {}

Screen& current_screen() {
    static Screen instance{};
    return instance;
}

void screen_install(ScreenHandler handler, ScreenTick tick, ScreenRender render,
                    ScreenEnter next_enter) {
    current_screen().handler = handler;
    current_screen().tick = tick;
    current_screen().render = render;
    current_screen().next_enter = next_enter;
}

// 0x180047cc — play one of the menu's sounds (move, select, back), if sound is on at all and
// that particular one is enabled.
void menu_sound_play(uint32_t sound) {
    // The original tested a flag byte and, only when it was set, cleared a word and checked the
    // range; with the flag clear it skipped everything. Both read the same way here.
    if (static_cast<uint32_t>(game_state_block().byte_83009) == 0) {
        return;
    }
    play_state().word_7e8 = 0;
    if (sound >= MENU_SOUND_COUNT || play_state().sound_enabled[sound] == 0) {
        return;
    }
    if (sound == 0) {  // sound 0 only records that it was asked for
        play_state().word_7d8 = 0;
        play_state().word_7e8 = MENU_SOUND_COUNT;
        return;
    }
    play_state().word_7d8 = MENU_SOUND_COUNT;
    const uint32_t zero = device::clock_reserved();
    const uint32_t thousand = device::clock_rate();
    sound_effect_play(play_state().slot, sound, VOICE, thousand, zero);
}

uint32_t screen_handle_event(uint32_t event) {
    const ScreenHandler handler = current_screen().handler;
    if (handler == nullptr) {
        assert_trap(0x180068a0u);
    }
    return handler(event);
}

namespace {

void screen_tick(uint32_t milliseconds, uint32_t trap) {
    const ScreenTick tick = current_screen().tick;
    if (tick == nullptr) {
        assert_trap(trap);
    }
    tick(milliseconds);
}

// Items' style bytes live in the menu table; style 3 marks a heading row the cursor skips.
uint32_t menu_item_style(int32_t item) {
    return menu_table()[item].style;
}

int8_t clamp_cursor(int32_t cursor, int32_t count, bool moving_up) {
    if (moving_up) {
        return static_cast<int8_t>(cursor < 0 ? 0 : cursor);
    }
    return static_cast<int8_t>(cursor == count ? count - 1 : cursor);
}

// The first two screens demonstrate themselves: a timer in the settings, advanced by a word
// from the player table, moves the cursor one row when it expires.
bool menu_demo_due(uint32_t selection) {
    const uint32_t step = wheel_slot_at(selection == 0 ? 0 : 1).step;
    const bool continuing =
        static_cast<uint32_t>(play_state().wheel_direction) == (selection == 0 ? 0 : 1);
    const uint32_t timer = continuing ? play_state().wheel_repeat + step : step;
    play_state().wheel_repeat = timer;
    play_state().wheel_direction = static_cast<uint8_t>(selection == 0 ? 0 : 1);
    if (static_cast<int32_t>(timer) < static_cast<int32_t>(play_state().wheel_repeat_limit)) {
        return false;
    }
    play_state().wheel_repeat = 0;
    return true;
}

// Move the cursor one row, skipping a heading row, and scroll the visible window after it.
// Returns whether it moved.
bool menu_cursor_move(bool moving_up) {
    const int32_t old_cursor = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    const int32_t count = static_cast<int32_t>(static_cast<int8_t>(menu_state().item_count));
    const int32_t step = moving_up ? -1 : 1;
    menu_state().cursor = static_cast<int8_t>(static_cast<uint8_t>(
        static_cast<uint32_t>(clamp_cursor(old_cursor + step, count, moving_up))));
    if (menu_item_style(static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor))) == 3) {
        const int32_t cursor = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
        menu_state().cursor = static_cast<int8_t>(static_cast<uint8_t>(
            static_cast<uint32_t>(clamp_cursor(cursor + step, count, moving_up))));
        const int32_t again = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
        if (menu_item_style(again) == 3) {
            menu_state().cursor = static_cast<int8_t>(
                static_cast<uint8_t>(static_cast<uint32_t>(static_cast<int8_t>(again - step))));
        }
    }
    const int32_t cursor = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    const int32_t first = static_cast<int32_t>(static_cast<uint32_t>(menu_state().first_row));
    const int32_t rows = static_cast<int32_t>(static_cast<uint32_t>(menu_state().visible_rows));
    if (moving_up && cursor < first) {
        menu_state().first_row = static_cast<int8_t>(static_cast<uint32_t>(cursor));
    }
    if (!moving_up && first + rows <= cursor) {
        menu_state().first_row = static_cast<int8_t>(static_cast<uint32_t>(first + 1));
    }
    return cursor != old_cursor;
}

}  // namespace

// 0x18006cb4 — the title-style wrapper: run the tick; answer 0.
uint32_t plain_screen_step(uint32_t milliseconds) {
    screen_tick(milliseconds, 0x18006cccu);
    return 0;
}

// 0x18006688 — the menu wrapper: run the tick, then act on the selection once the menu is
// steady. Answers what the handler answered (1 asks the flow to suspend).
uint32_t menu_screen_step(uint32_t milliseconds) {
    uint32_t result = 0;
    screen_tick(milliseconds, 0x180066a0u);
    if (static_cast<uint32_t>(screen_state().phase) == PHASE_STEADY) {
        switch (static_cast<uint32_t>(text_block().selection)) {
        case 0:
        case 1: {
            const bool moving_up = static_cast<uint32_t>(text_block().selection) == 0;
            if (menu_demo_due(moving_up ? 0 : 1) && menu_cursor_move(moving_up)) {
                menu_sound_play(1);
            }
            break;
        }
        case 5:
            result = screen_handle_event(EVENT_SELECT);
            menu_sound_play(4);
            break;
        case 6:
            result = screen_handle_event(EVENT_MENU);
            menu_sound_play(2);
            break;
        default:
            break;
        }
    }
    return result;
}

}  // namespace minigolf::game
