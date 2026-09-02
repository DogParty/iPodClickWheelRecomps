// The program's entry points: what the firmware calls once at start-up and once per frame.
//
// The eApp header's vector table names three functions. `app_entry` (0x180188c0) is armcc's
// start-up: zero the BSS, initialise the C library, run the static constructors. The second
// vector (0x180188bc) does nothing. `app_frame` (0x1801891c) is the real main-loop body the
// firmware drives at 60 Hz: read the clock, poll the click wheel, maintain the tap detector,
// dispatch buttons, update the game, measure the frame, handle long-press suspend, and swap.
//
// These were the third and fourth functions decompiled, chosen so the program reads as a game
// loop rather than as vectors being called; the context and input structures they use are named
// in game_state.h. Register and stack conventions for talking to still-recompiled code are in
// calling.h.
#include "calling.h"
#include "file_objects.h"
#include "flow.h"
#include "frame.h"
#include "framework/controls.h"
#include "framework/device.h"
#include "framework/graphics.h"
#include "game_state.h"
#include "gl.h"
#include "init.h"
#include "input.h"
#include "libc.h"
#include "random.h"
#include "records.h"
#include "runtime/cpu.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "save.h"
#include "shims.h"
#include "sound_bank.h"
#include "state.h"

#include <cstdlib>
#include <cstring>

namespace minigolf::game {

namespace {

// Still recompiled, named by what they do here (inferred).

constexpr uint32_t MICROSECONDS_PER_MILLISECOND = 1000;
constexpr uint32_t WHEEL_POSITIONS = 0x100;  // the position byte wraps at 256
constexpr uint32_t WHEEL_FAST = 0x2d0, WHEEL_BRISK = 0x168, WHEEL_FLICK = 10;
constexpr uint32_t STEP_RESULT_STATE_CHANGED = 2, STEP_RESULT_SUSPEND = 5;

// Milliseconds elapsed this step, carrying the sub-millisecond remainder between steps.
uint32_t milliseconds_elapsed(uint32_t delta_microseconds) {
    const uint32_t carried =
        delta_microseconds % MICROSECONDS_PER_MILLISECOND + app_state().ms_remainder;
    app_state().ms_remainder = carried % MICROSECONDS_PER_MILLISECOND;
    return delta_microseconds / MICROSECONDS_PER_MILLISECOND +
           carried / MICROSECONDS_PER_MILLISECOND;
}

// Signed wheel travel since the last step, with acceleration: a fast flick counts double, a
// brisk one one-and-a-half times, and travel of ten or more positions builds up the speed.
int32_t wheel_movement(uint32_t flags, uint32_t position) {
    if (!(flags & input::FLAG_WHEEL_EVENT)) {
        app_state().last_wheel_position = 0xffff'ffffu;
        app_state().wheel_speed = 0;
        return 0;
    }
    const int32_t last = static_cast<int32_t>(app_state().last_wheel_position);
    app_state().last_wheel_position = position;
    if (last < 0 || static_cast<int32_t>(position) == last) {
        return 0;
    }
    uint32_t distance = static_cast<uint32_t>(std::abs(static_cast<int32_t>(position) - last));
    int32_t direction = static_cast<int32_t>(position) > last ? -1 : 1;
    if (WHEEL_POSITIONS - 1 - distance < distance) {  // the short way round the wheel
        distance = WHEEL_POSITIONS - 1 - distance;
        direction = -direction;
    }
    const uint32_t speed = app_state().wheel_speed;
    if (speed > WHEEL_FAST) {
        distance *= 2;
    } else if (speed > WHEEL_BRISK) {
        distance = (distance * 3 + ((distance * 3) >> 31)) / 2;  // the original's rounding
    } else {
        app_state().wheel_speed = distance >= WHEEL_FLICK ? speed + distance : 0;
    }
    return direction * static_cast<int32_t>(distance);
}

// A button held down counts once: after its first frame the flag is hidden from the game and
// its hold time accumulates instead (used for long presses).
uint32_t filter_held_buttons(uint32_t flags, uint32_t milliseconds) {
    for (uint32_t i = 0; i < BUTTON_HOLD_COUNT; ++i) {
        uint32_t& hold_time = guest_array<uint32_t>(BUTTON_HOLD_TIMES)[i];
        const uint32_t bit = 1u << i;
        if (!(flags & bit)) {
            hold_time = 0;
            continue;
        }
        const int32_t held = static_cast<int32_t>(hold_time);
        if (held > 0) {
            hold_time = static_cast<uint32_t>(held) + milliseconds;
            flags &= ~bit;
        } else {
            hold_time = 1;
        }
    }
    return flags;
}

// 0x1800e488 — one step of the running game: time, wheel, buttons, then the state machine.
void game_step(uint32_t delta_microseconds) {
    ContextBlock& context = as_context(app_state().context);
    prepare_step_input(step_input(), context.firmware_word_14, context.firmware_word_18);
    input_snapshot_read(step_input());

    if (app_state().mode == 2) {
        const uint32_t milliseconds = milliseconds_elapsed(delta_microseconds);
        const int32_t movement =
            wheel_movement(step_input().flags, step_input().wheel_position >> 16);
        wheel_movement_apply(movement);

        uint32_t flags = step_input().flags & ~input::FLAG_WHEEL_TAPPED;
        step_input().flags = flags;
        flags = filter_held_buttons(flags, milliseconds);
        step_input().flags = flags;
        buttons_apply(flags);

        const uint32_t step = flow_step(milliseconds);
        if (step == STEP_RESULT_STATE_CHANGED) {
            app_state().state = static_cast<uint8_t>(1);
        } else if (step == STEP_RESULT_SUSPEND) {
            as_answer(app_state().answer).state = static_cast<uint8_t>(STEP_RESULT_SUSPEND);
        }
    }
    input_snapshot_clear_flags();
    input_snapshot_clear_movement();
}

// 0x1800cb70 — the firmware asked to suspend: leave the running mode and report suspended.
void suspend(AnswerBlock& answer) {
    app_state().mode = 4;
    answer.state = static_cast<uint8_t>(firmware_state::ANSWER_SUSPENDED);
    app_exit();  // the suspend hook (0x180135e4) does nothing and answers 1
}

// 0x18015188 — the tick: act on what the firmware's state byte asks. Always returns 1.
void game_tick(uint32_t clock) {
    const uint32_t delta = clock - app_state().last_tick_clock;
    app_state().last_tick_clock = clock;
    app_state().frame_delta = delta;
    app_state().state = static_cast<uint8_t>(0);
    const uint32_t context = app_state().context;
    AnswerBlock& answer = as_answer(app_state().answer);
    const uint32_t state = static_cast<uint32_t>(as_context(context).state);
    app_state().firmware_state = static_cast<uint8_t>(state);

    switch (state) {
    case firmware_state::INITIALISE: {
        uint32_t seed = clock ^ RANDOM_SEED_MIX;
        if (seed == 0) {
            seed = RANDOM_SEED_MIX;
        }
        // Two set-up hooks (0x1801140c, 0x180135ec) do nothing but return 1 in this build.
        random_seed(seed);
        game_init();
        app_state().mode = 2;
        app_state().firmware_state = static_cast<uint8_t>(firmware_state::ANSWER_RUNNING);
        answer.state = static_cast<uint8_t>(firmware_state::ANSWER_RUNNING);
        break;
    }
    case firmware_state::RUN:
        game_step(delta);
        break;
    case firmware_state::SUSPEND_FIRST:
    case firmware_state::SUSPEND_FIRST + 1:
    case firmware_state::SUSPEND_LAST:
        suspend(answer);
        break;
    case firmware_state::SUSPENDED:
        break;
    default:
        answer.state = static_cast<uint8_t>(firmware_state::ANSWER_UNKNOWN_STATE);
        break;
    }
}

constexpr uint32_t FADE_STEP = 0x3d08'8889;  // 1/30 as a float: the intro fades in over 30 frames

uint32_t float_add(uint32_t a_bits, uint32_t b_bits) {
    float a, b;
    std::memcpy(&a, &a_bits, sizeof a);
    std::memcpy(&b, &b_bits, sizeof b);
    const float sum = a + b;
    uint32_t sum_bits;
    std::memcpy(&sum_bits, &sum, sizeof sum_bits);
    return sum_bits;
}

// 0x18011538 — the per-frame update: two start-up frames (a fade from black, then a plain
// clear) and from then on the game tick. Always returns 1.
void game_update(uint32_t context, uint32_t answer) {
    // The original's frame — four registers and 32 bytes of locals — kept for the whole
    // function: the game passes stack addresses around (glOrtho gets a matrix on the stack),
    // so every callee must see sp exactly where it was.
    GuestScratch frame(4 * 4 + 0x20);
    app_state().context = context;
    app_state().answer = answer;
    const uint32_t frame_count = app_state().frame_count;

    if (frame_count == 0) {
        // Fade in: the clear colour's red channel climbs by 1/30 a frame until it wraps.
        const uint32_t fade = app_state().fade;
        gfx::set_clear_color(fade, 0, 0, FLOAT_ONE);
        uint32_t next_fade = float_add(fade, FADE_STEP);
        if (static_cast<int32_t>(next_fade) >= static_cast<int32_t>(FLOAT_ONE)) {
            next_fade = 0;
        }
        app_state().fade = next_fade;
        gfx::clear(gfx::Buffer::Color);

        // Select skips straight past the fade.
        InputSnapshot& snapshot_copy = as_snapshot(frame.at(4));
        input_snapshot_read(snapshot_copy);
        const uint32_t flags = snapshot_copy.flags;
        input_snapshot_clear_flags();
        input_snapshot_clear_movement();
        if (flags & input::FLAG_SELECT) {
            app_state().frame_count = 1;
        }
        return;
    }

    if (frame_count == 1) {
        gfx::set_clear_color(0, 0, 0, FLOAT_ONE);
        gfx::clear(gfx::Buffer::Color);
    } else {
        game_tick(as_context(context).clock);
        if (static_cast<uint32_t>(app_state().state) != 0 && app_state().mode == 2) {
            guest<uint32_t>(MODE_CHANGED_FLAG) = 2;
            // The original asked and ignored the answer; it is a capability word.
            (void)gfx::pipeline_capabilities(0x3f001, app_state().pipeline_arg);
            frame_render();
        }
    }
    app_state().frame_count = frame_count + 1;
}

constexpr uint32_t SUSPEND_REQUESTED = 5;
constexpr uint32_t SUSPENDED = 6;
constexpr uint32_t HOLD_NONE = 0, HOLD_MENU = 1, HOLD_NEXT = 2;

// Record this frame's wheel contact in the rings and return how many of the last sixteen
// frames began a fresh touch — the game's tap detector.
uint32_t record_wheel_contact(bool contact) {
    const uint32_t slot = input_state().ring_index;
    const uint32_t previous = (slot - 1) & (WHEEL_RING_SIZE - 1);
    const bool touch_began = contact && guest_array<uint32_t>(WHEEL_CONTACT_RING)[previous] == 0;
    guest_array<uint32_t>(WHEEL_CONTACT_RING)[slot] = contact ? 1 : 0;
    guest_array<uint32_t>(WHEEL_TOUCH_BEGIN_RING)[slot] = touch_began ? 1 : 0;

    uint32_t touches = 0;
    for (uint32_t i = 0; i < WHEEL_RING_SIZE; ++i) {
        if (guest_array<uint32_t>(WHEEL_TOUCH_BEGIN_RING)[(slot + i) & (WHEEL_RING_SIZE - 1)] !=
            0) {
            ++touches;
        }
    }
    input_state().ring_index = (slot + 1) & (WHEEL_RING_SIZE - 1);
    return touches;
}

// A button held past its limit asks the firmware to suspend (as_answer(answer state 5)); when the
// firmware reports the suspend (as_answer(state 6)), the resume flag for that button is raised
// instead.
void handle_long_presses(ContextBlock& context, AnswerBlock& answer) {
    const uint32_t hold = static_cast<uint32_t>(input_state().hold_state);
    const uint32_t clock = context.clock;
    const uint32_t flags = input_state().flags;
    if (hold == HOLD_NONE) {
        if (static_cast<uint32_t>(answer.state) == SUSPENDED) {
            return;
        }
        if ((flags & input::FLAG_MENU) &&
            clock - input_state().menu_press_time > input_state().menu_hold_limit) {
            input_state().hold_state = static_cast<uint8_t>(HOLD_MENU);
            answer.state = static_cast<uint8_t>(SUSPEND_REQUESTED);
        }
        if ((flags & input::FLAG_NEXT) &&
            clock - input_state().next_press_time > input_state().next_hold_limit) {
            input_state().hold_state = static_cast<uint8_t>(HOLD_NEXT);
            answer.state = static_cast<uint8_t>(SUSPEND_REQUESTED);
        }
    } else if (static_cast<uint32_t>(answer.state) == SUSPENDED) {
        if (hold == HOLD_MENU) {
            answer.resume_flag_b = static_cast<uint8_t>(1);
        } else if (hold == HOLD_NEXT) {
            answer.resume_flag_a = static_cast<uint8_t>(1);
        }
    }
}

}  // namespace

// 0x180188c0 — armcc's program start-up, the first vector the firmware calls.
void f_180188c0(Cpu& /*cpu*/) {
    if (BSS_SIZE != 0) {
        libc::memory_clear(BSS_START, BSS_SIZE);  // memclr
    }
    // The C library's initialisation (0x18000e84) set up its descriptor from a name string the
    // game never varies and allocated nothing the game can see; nothing of it is needed here.
    // Then the static constructors, in the order of the image's table (0x1801a500).
    file_objects_construct();
    request_records_construct();
    sound_bank_construct(SOUND_BANK);
}

// 0x180188bc — the second vector: nothing to do.
void f_180188bc(Cpu& /*cpu*/) {}

// 0x1801891c — one frame of the game, called by the firmware (here: runtime/main.cpp) at 60 Hz
// with (context, context + 0x100).
void f_1801891c(Cpu& cpu) {
    // The original pushed r2 and r3 purely to make room for the input poll's two result words.
    GuestScratch frame(4 * 10);
    ContextBlock& context = as_context(cpu.r[0]);
    AnswerBlock& answer = as_answer(cpu.r[1]);

    (void)device::clock_microseconds(field_address(context, offsetof(ContextBlock, clock)));
    if (static_cast<uint32_t>(answer.state) == 0) {  // first frame: start the timers now
        const uint32_t now = context.clock;
        input_state().menu_press_time = now;
        input_state().next_press_time = now;
        input_state().last_clock = now;
        answer.button_event = 0;
        context.word_34 = 0;
    }

    const uint32_t wheel_delta_slot = frame.at(4 * 0);
    const uint32_t wheel_event_slot = frame.at(4 * 1);
    controls::poll(wheel_delta_slot, wheel_event_slot);
    const uint32_t wheel_event = guest<uint32_t>(wheel_event_slot);
    const bool contact = (wheel_event & EVENT_PRESENT) != 0;
    if (contact) {
        wheel_position_update(wheel_event & 0xff);
    }
    const uint32_t recent_touches = record_wheel_contact(contact);

    input_state().flags = dispatch_buttons(context.event_list_head, input_state().flags,
                                           context.clock, answer.button_event);

    uint32_t flags = input_state().flags & ~(input::FLAG_WHEEL_EVENT | input::FLAG_WHEEL_TAPPED);
    if (recent_touches > 1) {
        flags |= input::FLAG_WHEEL_TAPPED;
    }
    if (contact) {
        flags |= input::FLAG_WHEEL_EVENT;
    }
    input_state().flags = flags;
    if (flags != 0) {
        input_snapshot_store(flags, context.event_list_head);
        input_state().last_clock = context.clock;
    }

    release_completed_requests(context.completed_requests);
    context.completed_requests = 0;
    answer.resume_flag_a = static_cast<uint8_t>(0);
    answer.idle = 0;
    answer.idle_answer = 0;
    answer.resume_flag_b = static_cast<uint8_t>(0);
    game_update(address_of(context), address_of(answer));

    (void)device::clock_microseconds(
        field_address(context, offsetof(ContextBlock, frame_duration)));
    context.frame_duration = context.frame_duration - context.clock;

    handle_long_presses(context, answer);
    context.state = static_cast<uint8_t>(static_cast<uint32_t>(answer.state));
    gfx::swap_buffers();  // swap buffers: the frame is done
}

}  // namespace minigolf::game
