// The click-wheel input layer. See input.h for the functions and game_state.h for the objects.
//
// The firmware reports the wheel as a position byte each frame and the five buttons as events
// on a list (type = which button, state = pressed or released). The game keeps a word of
// button flags instead, so `dispatch_buttons` turns events into flag edges — with one wrinkle:
// a press and release inside the same frame would cancel out, so such a button is held for one
// frame and its release is replayed on the next (`input::RELEASE_NEXT_FRAME`).
#include "input.h"

#include "calling.h"
#include "fixed.h"
#include "framework/controls.h"
#include "game_state.h"
#include "records.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"

namespace minigolf::game {

namespace {

constexpr uint32_t EVENT_RELEASED = 1;
constexpr uint32_t EVENT_PRESSED = 2;

// The button a firmware event names, as its flag bit. Types 1..5 are Menu, Select, Previous,
// Play, Next; anything else is not a button.
uint32_t button_flag(uint32_t event_type) {
    switch (event_type) {
    case 1:
        return input::FLAG_MENU;
    case 2:
        return input::FLAG_SELECT;
    case 3:
        return input::FLAG_PREVIOUS;
    case 4:
        return input::FLAG_PLAY;
    case 5:
        return input::FLAG_NEXT;
    default:
        return 0;
    }
}

}  // namespace

void wheel_position_update(uint32_t position) {
    const uint32_t fixed = to_fixed(position);
    guest<uint32_t>(WHEEL_POSITION) = fixed;
    guest<uint32_t>(WHEEL_POSITION_COPY) = fixed;
}

void input_snapshot_store(uint32_t flags, uint32_t event_list) {
    input_snapshot().flags = flags;
    input_snapshot().event_list = event_list;
}

void input_snapshot_read(InputSnapshot& destination) {
    destination.wheel_position = input_snapshot().wheel_position;
    destination.wheel_movement = input_snapshot().wheel_movement;
    destination.flags = input_snapshot().flags;
    destination.event_list = input_snapshot().event_list;
}

void input_snapshot_clear_flags() {
    input_snapshot().flags = 0;
}

void input_snapshot_clear_movement() {
    input_snapshot().wheel_movement = 0;
}

uint32_t dispatch_buttons(uint32_t event_list, uint32_t button_flags, uint32_t clock,
                          uint32_t pressed_mask) {
    const uint32_t flags_at_entry = button_flags;

    // Releases deferred from last frame happen first.
    for (uint32_t bit = 1; bit <= input::FLAG_MENU; bit <<= 1) {
        const uint32_t deferred = input_state().release_next_frame;
        if (deferred & bit) {
            button_flags = button_flags ^ bit;
            input_state().release_next_frame = deferred & ~bit;
        }
    }

    // Then this frame's events, in list order.
    uint32_t touched = 0;
    for (uint32_t node = event_list; node != 0; node = as_event(node).next) {
        const uint32_t state = as_event(node).state;
        uint32_t bit = 0;
        if (state == EVENT_PRESSED) {
            bit = button_flag(as_event(node).type);
            if (bit == input::FLAG_NEXT) {
                input_state().next_press_time = clock;
            } else if (bit == input::FLAG_MENU) {
                input_state().menu_press_time = clock;
            }
            button_flags = button_flags | bit;
        } else if (state == EVENT_RELEASED) {
            bit = button_flag(as_event(node).type);
            button_flags = button_flags & ~bit;
        }
        if (state == EVENT_PRESSED || state == EVENT_RELEASED) {
            if (bit != input::FLAG_MENU && (pressed_mask & bit)) {
                (void)controls::release_event(node);  // the host owns nothing to free
            }
        }
        touched |= bit;
    }

    // A button that ended the frame in the state it started in was pressed and released within
    // it: flip it now and flip it back next frame, so the game sees a one-frame press.
    for (uint32_t bit = 1; bit <= input::FLAG_MENU; bit <<= 1) {
        if (!(touched & bit)) {
            continue;
        }
        const uint32_t flags = button_flags;
        if ((flags & bit) == (flags_at_entry & bit)) {
            input_state().release_next_frame = input_state().release_next_frame | bit;
            button_flags = flags ^ bit;
        }
    }
    return button_flags;
}

void release_request(EventNode& request) {
    request.in_use = 0;
    if (request.owner == 0) {
        const uint32_t result = request.result;
        const uint32_t next = request.next;
        if (result != 0xffff'ffffu && next != 0) {
            as_event(next).next = result;
        }
    }
    const uint32_t callback = request.callback;
    if (callback != 0) {
        // A tail call in the original: the callback gets (request, context) and returns
        // straight to our caller, so it sees our caller's return address.
        call_indirect(callback, {address_of(request), request.context});
    }
}

void release_completed_requests(uint32_t list) {
    for (uint32_t node = list; node != 0;) {
        const uint32_t next = as_event(node).next;
        as_event(node).next = 0;
        release_request(as_event(node));
        node = next;
    }
}

void wheel_movement_apply(int32_t movement) {
    input_snapshot().wheel_movement = movement;
}

void buttons_apply(uint32_t flags) {
    input_snapshot().flags = flags;
}

void prepare_step_input(InputSnapshot& copy, uint32_t word_a, uint32_t word_b) {
    copy.firmware_words[0] = word_a;
    copy.firmware_words[1] = word_b;
}

// --- reached from the frame pump and the dispatch table ----------------------------------------

// 0x1800dcec — one wheel slot (PLAYER_TABLE, eight bytes: flags halfword, count halfword,
// value word) from this frame's input: `pressed` with its `value`. The flags: bit 0 held
// last frame, bit 1 held now, bit 2 just pressed, bit 3 just released; the count grows while
// held and restarts on a press.
void wheel_slot_set(uint32_t index, uint32_t pressed, uint32_t value) {
    if (index >= WHEEL_SLOT_COUNT) {
        assert_trap(0x1800dcf4u);
    }
    WheelSlot& slot = wheel_slot_at(index);
    slot.flags = static_cast<uint16_t>(pressed == 0 ? slot.flags & ~2u : slot.flags | 2u);
    slot.step = pressed == 0 ? 0 : value;
    if ((slot.flags & 1) != 0) {  // held last frame
        if (pressed == 0) {
            slot.count = static_cast<uint16_t>(slot.count + 1);
            return;
        }
        slot.flags = static_cast<uint16_t>((slot.flags & ~9u) | 4u);
        slot.count = 1;
        return;
    }
    if (pressed != 0) {
        slot.count = static_cast<uint16_t>(slot.count + 1);
    } else {
        slot.flags = static_cast<uint16_t>(slot.flags | 9u);
        slot.count = 0;
    }
    slot.flags = static_cast<uint16_t>(slot.flags & ~4u);
}

// 0x180082c4 — this frame's input into the wheel slots: the wheel's movement as the two
// direction slots (each with its step), the five buttons from the flags, and the flags
// cleared for the next frame. A movement this frame wins over a held direction.
void input_gather() {
    GuestScratch frame(4 * 5 + 0x1c);
    InputSnapshot& snapshot = as_snapshot(frame.at(0));
    input_snapshot_read(snapshot);
    const int32_t movement = snapshot.wheel_movement;
    const uint32_t flags = snapshot.flags;
    int32_t magnitude = movement, direction = 0;
    if (movement < 0) {
        magnitude = -movement;
        direction = 1;
    } else if (movement > 0) {
        direction = -1;
    }
    input_snapshot_clear_flags();
    input_snapshot_clear_movement();
    wheel_slot_set(2, flags & 0x4, 1);
    wheel_slot_set(3, flags & 0x8, 1);
    wheel_slot_set(4, flags & 0x2, 1);
    wheel_slot_set(5, flags & 0x1, 1);
    wheel_slot_set(6, flags & 0x10, 1);
    wheel_slot_set(0, 0, 0);
    wheel_slot_set(1, 0, 0);
    if ((flags & 0x20) == 0) {
        return;
    }
    const int32_t signed_step = direction * -magnitude;
    if (signed_step > 0) {
        wheel_slot_set(1, 1, static_cast<uint32_t>(signed_step));
        wheel_slot_set(0, 0, 0);
    } else {
        wheel_slot_set(0, signed_step < 0 ? 1u : 0u,
                       static_cast<uint32_t>(signed_step < 0 ? -signed_step : 0));
        wheel_slot_set(1, 0, 0);
    }
}

void wheel_slots_clear() {
    for (uint32_t i = 0; i < WHEEL_SLOT_COUNT; ++i) {
        wheel_slot_at(i) = WheelSlot{};
    }
}

}  // namespace minigolf::game
