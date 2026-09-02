// The per-frame ticks of the screens that are a menu with something in front of it: the
// course select strip, a page, a dialog and the name entry. Each runs the menu tick, then
// reads the frame's input (TEXT_SELECTION: 0 and 1 the wheel, 5 Select, 6 Menu) — the wheel
// moves the highlighted line once its clicks add up to a repeat, Select and Menu start the
// panel shrinking and remember which was pressed for the handler to act on when it is gone.
#include "calling.h"
#include "game_state.h"
#include "menu.h"
#include "name_entry.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "shims.h"
#include "state.h"

namespace minigolf::game {

void music_level_adjust();
uint32_t brightness_adjust();

namespace {

constexpr uint32_t PANEL_SHRINK_STEP = 0xffff'e667;  // -0x199a: the panel goes as it came
constexpr uint32_t SOUND_MOVE = 1, SOUND_BACK = 2, SOUND_SELECT = 4;

uint32_t frame_input() {
    return static_cast<uint32_t>(text_block().selection);
}
int32_t line() {
    return static_cast<int32_t>(
        static_cast<uint32_t>(static_cast<int8_t>(text_block().carousel_course)));
}
void line_set(int32_t value) {
    text_block().carousel_course = static_cast<uint8_t>(static_cast<uint32_t>(value));
}

// The wheel's clicks in one direction add up in WHEEL_REPEAT; a change of direction starts
// over. Returns the total so far.
int32_t wheel_accumulate(uint32_t direction) {
    uint32_t repeat = wheel_slot_at(direction).step;
    if (static_cast<uint32_t>(play_state().wheel_direction) == direction) {
        repeat += play_state().wheel_repeat;
    }
    play_state().wheel_repeat = repeat;
    play_state().wheel_direction = static_cast<uint8_t>(direction);
    return static_cast<int32_t>(repeat);
}
int32_t wheel_limit() {
    return static_cast<int32_t>(play_state().wheel_repeat_limit);
}

// Whether the clicks reached the repeat limit; if so they start again from nothing.
bool wheel_repeat(uint32_t direction) {
    if (wheel_accumulate(direction) < wheel_limit()) {
        return false;
    }
    play_state().wheel_repeat = 0;
    return true;
}

void panel_shrink_begin(uint32_t pressed) {
    play_state().panel_growing = static_cast<uint8_t>(1);
    play_state().panel_scale_step = PANEL_SHRINK_STEP;
    play_state().panel_pressed = static_cast<uint8_t>(pressed);
}

// While the panel is still growing nothing happens; once it has shrunk away, the screen's
// handler gets the button that closed it. Returns whether the tick is over.
bool panel_settle(uint32_t& result) {
    if (static_cast<uint32_t>(play_state().panel_growing) != 0) {
        result = 0;
        return true;
    }
    if (static_cast<int32_t>(play_state().panel_scale_step) >= 0) {
        return false;
    }
    const ScreenHandler handler = current_screen().handler;
    if (handler == nullptr) {
        return false;
    }
    result = handler(static_cast<uint32_t>(play_state().panel_pressed));
    return true;
}

void menu_tick_run(uint32_t milliseconds, uint32_t assert_at) {
    const ScreenTick tick = current_screen().tick;
    if (tick == nullptr) {
        assert_trap(assert_at);
    }
    tick(milliseconds);
}

uint32_t handler_run(uint32_t event) {
    const ScreenHandler handler = current_screen().handler;
    if (handler == nullptr) {
        return 0;
    }
    return handler(event);
}

}  // namespace

// 0x18006ce4 — a page's tick: the volume and brightness pages give the wheel to their level;
// the others scroll the lines.
uint32_t page_tick(uint32_t milliseconds) {
    uint32_t result = 0;
    if (panel_settle(result)) {
        return result;
    }
    menu_tick_run(milliseconds, 0x18006d34u);
    const int32_t page = static_cast<int32_t>(static_cast<uint32_t>(menu_state().page));
    switch (frame_input()) {
    case WHEEL_CLOCKWISE:
    case WHEEL_COUNTER: {
        if (page == PAGE_VOLUME) {
            music_level_adjust();
            return 0;
        }
        if (page == PAGE_BRIGHTNESS) {
            brightness_adjust();
            return 0;
        }
        const bool up = frame_input() == WHEEL_CLOCKWISE;
        if (!wheel_repeat(up ? WHEEL_CLOCKWISE : WHEEL_COUNTER)) {
            return 0;
        }
        const int32_t before = line();
        if (up) {
            line_set(before - 1 < 0 ? 0 : before - 1);
        } else {
            const int32_t last =
                static_cast<int32_t>(
                    static_cast<uint32_t>(static_cast<int8_t>(text_block().carousel_count))) -
                static_cast<int32_t>(
                    static_cast<uint32_t>(static_cast<int8_t>(text_block().score_card_rows_shown)));
            line_set(before + 1 > last ? last : before + 1);
            if (line() < 0) {
                line_set(0);
            }
        }
        if (line() != before) {
            menu_sound_play(SOUND_MOVE);
        }
        return 0;
    }
    case EVENT_SELECT:
        menu_sound_play(SOUND_SELECT);
        panel_shrink_begin(EVENT_SELECT);
        return 0;
    case EVENT_MENU:
        menu_sound_play(SOUND_BACK);
        panel_shrink_begin(EVENT_MENU);
        return 0;
    default:
        return 0;
    }
}

// 0x18004b4c — a dialog's tick: the wheel moves between its two answers; Select takes the
// highlighted one (the first closes as Select, the second as Menu); Menu closes as Menu
// unless the dialog is one whose Menu means the other thing.
uint32_t dialog_tick(uint32_t milliseconds) {
    uint32_t result = 0;
    if (panel_settle(result)) {
        return result;
    }
    menu_tick_run(milliseconds, 0x18004b98u);
    switch (frame_input()) {
    case WHEEL_CLOCKWISE:
    case WHEEL_COUNTER: {
        const bool up = frame_input() == WHEEL_CLOCKWISE;
        if (!wheel_repeat(up ? WHEEL_CLOCKWISE : WHEEL_COUNTER)) {
            return 0;
        }
        const int32_t before = line();
        if (up) {
            line_set(before - 1 < 0 ? 0 : before - 1);
        } else {
            line_set(before + 1 == 2 ? 1 : before + 1);
        }
        if (line() != before) {
            menu_sound_play(SOUND_MOVE);
        }
        return 0;
    }
    case EVENT_SELECT:
        menu_sound_play(SOUND_SELECT);
        if (line() == 0) {
            panel_shrink_begin(EVENT_SELECT);
        } else {
            menu_sound_play(SOUND_MOVE);
            panel_shrink_begin(EVENT_MENU);
        }
        return 0;
    case EVENT_MENU: {
        menu_sound_play(SOUND_MOVE);
        const int32_t type = static_cast<int32_t>(
            static_cast<uint32_t>(static_cast<int8_t>(menu_state().dialog_type)));
        panel_shrink_begin(type == 2 || type == 3 || type == 6 ? EVENT_MENU : EVENT_SELECT);
        return 0;
    }
    default:
        return 0;
    }
}

// 0x180068fc — the name entry's tick: the wheel runs along the alphabet (a fast spin skips
// several letters at once), Select and Menu go straight to the handler.
uint32_t name_entry_tick(uint32_t milliseconds) {
    uint32_t result = 0;
    if (panel_settle(result)) {
        return result;
    }
    if (name_entry_typing()) {  // an addition: a keyboard, where the platform has one
        return 0;
    }
    menu_tick_run(milliseconds, 0x18006944u);
    switch (frame_input()) {
    case WHEEL_CLOCKWISE: {
        if (wheel_accumulate(WHEEL_CLOCKWISE) < wheel_limit()) {
            return 0;
        }
        // Every limit's worth of clicks is a letter, while the line stays past the fifth.
        const int32_t before = line();
        for (;;) {
            line_set(static_cast<int8_t>(line() - 1));
            const int32_t repeat = static_cast<int32_t>(play_state().wheel_repeat) - wheel_limit();
            play_state().wheel_repeat = static_cast<uint32_t>(repeat);
            if (repeat < wheel_limit() || line() < 5) {
                break;
            }
        }
        play_state().wheel_repeat = 0;
        if (line() < 0) {
            line_set(0);
        }
        if (line() != before) {
            menu_sound_play(SOUND_MOVE);
        }
        return 0;
    }
    case WHEEL_COUNTER: {
        if (wheel_accumulate(WHEEL_COUNTER) < wheel_limit()) {
            return 0;
        }
        const int32_t before = line();
        const int32_t count = static_cast<int32_t>(
            static_cast<uint32_t>(static_cast<int8_t>(text_block().carousel_count)));
        for (;;) {
            line_set(static_cast<int8_t>(line() + 1));
            const int32_t repeat = static_cast<int32_t>(play_state().wheel_repeat) - wheel_limit();
            play_state().wheel_repeat = static_cast<uint32_t>(repeat);
            if (repeat < wheel_limit() || line() > count - 6) {
                break;
            }
        }
        play_state().wheel_repeat = 0;
        if (line() >= count) {
            line_set(count - 1);
        }
        if (line() != before) {
            menu_sound_play(SOUND_MOVE);
        }
        return 0;
    }
    case EVENT_SELECT:
        menu_sound_play(SOUND_SELECT);
        return handler_run(EVENT_SELECT);
    case EVENT_MENU:
        menu_sound_play(SOUND_BACK);
        return handler_run(EVENT_MENU);
    default:
        return 0;
    }
}

// 0x18006afc — the course select's tick: once the strip has settled, the wheel slides it to
// the course on either side; Select and Menu go to the handler.
uint32_t course_select_tick(uint32_t milliseconds) {
    menu_tick_run(milliseconds, 0x18006b14u);
    if (static_cast<uint32_t>(screen_state().phase) != PHASE_STEADY) {
        return 0;
    }
    switch (frame_input()) {
    case WHEEL_CLOCKWISE:
    case WHEEL_COUNTER: {
        const bool left = frame_input() == WHEEL_CLOCKWISE;
        if (!wheel_repeat(left ? WHEEL_CLOCKWISE : WHEEL_COUNTER)) {
            return 0;
        }
        const int32_t before = line();
        if (left) {
            line_set(before - 1 < 0 ? 0 : before - 1);
        } else {
            line_set(before + 1 == static_cast<int32_t>(COURSE_COUNT)
                         ? static_cast<int32_t>(COURSE_COUNT) - 1
                         : before + 1);
        }
        if (line() == before) {
            return 0;
        }
        menu_sound_play(SOUND_MOVE);
        current_screen().next_enter = 0;
        screen_state().phase = static_cast<uint8_t>(
            left ? PHASE_SLIDE_IN : PHASE_SLIDE_OUT);  // the strip slides either way
        text_block().frame_count = 0;
        return 0;
    }
    case EVENT_SELECT:
        menu_sound_play(SOUND_SELECT);
        return handler_run(EVENT_SELECT);
    case EVENT_MENU:
        menu_sound_play(SOUND_BACK);
        return handler_run(EVENT_MENU);
    default:
        return 0;
    }
}

// --- shims -----------------------------------------------------------------------------------

void f_18006ce4(Cpu& cpu) {
    cpu.r[0] = page_tick(cpu.r[0]);
}

void f_18004b4c(Cpu& cpu) {
    cpu.r[0] = dialog_tick(cpu.r[0]);
}

void f_180068fc(Cpu& cpu) {
    cpu.r[0] = name_entry_tick(cpu.r[0]);
}

void f_18006afc(Cpu& cpu) {
    cpu.r[0] = course_select_tick(cpu.r[0]);
}

}  // namespace minigolf::game
