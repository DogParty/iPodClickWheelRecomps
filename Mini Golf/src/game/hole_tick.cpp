// A hole being played: the state machine the hole screen runs every frame (0x18002c28) and
// the step that closes a hole out (0x18009c0c).
//
// The state is play::STATE. A hole goes: place the ball on the tee (1), aim (2), choose the
// power (3), the ball rolls (4, 5), and either sinks (6-8), comes to rest for the next stroke,
// or hits the ten-stroke limit (16, 17). Between strokes the hint sequence (10-13) and the
// messages (14, 15, 18, 19) run on the panel; 20 settles the hole, 21-23 show the score
// card, 24-25 the end-of-course message, 26-29 the course picture, 30 wipes to the next hole,
// and 31 is the "are you sure?" that Menu raises while aiming. Physics itself — one step of
// the ball (0x18009f28), what a tile does to it (0x1800ce04, 0x1800d1c4) — is still recompiled.
//
// Long because the state machine is: 32 states, and the transitions only make sense read
// together. The states themselves are in hole_tick.h, where the renderer and the turn code
// read them too.
#include "hole_tick.h"

#include "calling.h"
#include "cheats.h"
#include "course.h"
#include "fixed.h"
#include "game_state.h"
#include "hole_load.h"
#include "hole_render.h"
#include "libc.h"
#include "menu.h"
#include "pause_menu.h"
#include "physics.h"
#include "records.h"
#include "resources.h"
#include "round_history.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "state.h"
#include "strings.h"
#include "text.h"
#include "turn.h"

#include <algorithm>
#include <iterator>

namespace minigolf::game {

namespace {

// The end-of-course message's text id (RESULT_TEXT); -1 for none.
uint32_t& result_text() {
    return guest<uint32_t>(RESULT_TEXT);
}

constexpr uint32_t MS_PER_STEP = 0x21;
constexpr uint32_t SELECT = 5, MENU_BUTTON = 6;  // TEXT_SELECTION: which control fired
constexpr uint32_t TILE_TEE = 4;
constexpr uint32_t REST_STEPS = 10;
constexpr uint32_t HINT_FRAMES = 2, STRIKE_DELAY_STEPS = 10;
constexpr uint32_t QUARTER_TURN = 0x40;  // of the 256-step circle: cos(a) = sin(a + 90°)
constexpr uint32_t AIM_SPEEDS = 0x1803'79cc, AIM_SPEED_COUNT = 0x1803'4084 + 4;
constexpr uint32_t PANEL_STEP_IN = 0x199a, PANEL_STEP_OUT = 0xffff'e667u;
constexpr uint32_t FAST_FORWARD_STEPS = 100, TRAIL_SIZE = 64, ANIMATION_PHASES = 6;
constexpr uint32_t OBJECT_KIND_HIGH = 0x1000;
constexpr uint32_t SLIDE_FRAMES = 0x1e, WIPE_FRAMES = 0x3c;
constexpr uint32_t DROP_FRAMES_FIXED = to_fixed(30);
// Sounds and texts.
constexpr uint32_t SOUND_PUTT = 0, SOUND_HOLE_IN_ONE = 6, SOUND_UNDER_PAR = 7, SOUND_PAR = 8,
                   SOUND_OVER_PAR = 9;
constexpr uint32_t TEXT_UNDO_AIM = 0x7a, TEXT_UNDO_POWER = 0x79, TEXT_LIMIT = 0x5d, TEXT_OUT = 0x2e,
                   TEXT_PLAYER1_TURN = 0x54, TEXT_PLAYER2_TURN = 0x55, TEXT_UNLOCKED = 0x59,
                   TEXT_NICE_TRY = 0x57, TEXT_NICE_TRY_LAST = 0x58, TEXT_ALL_DONE = 0x65,
                   TEXT_P1_WINS = 0x5a, TEXT_P2_WINS = 0x5b, TEXT_TIE = 0x5c,
                   TEXT_HOLES_IN_ONE = 0x45, TEXT_TOTAL_STROKES = 0x7e,
                   TEXT_COURSE_NAME_FIRST = 0x25;
constexpr uint32_t LITERAL_NEWLINE_WIDE = 0x1800'3f2c, LITERAL_NEWLINE = 0x1800'3f30;
constexpr uint32_t LAYOUT_WIDTH = 0x80, LAYOUT_HEIGHT = 0x118;

// What the tick gathers at the top of a frame, and every state reads: how much time passed,
// what the player did with the wheel and buttons, and whose turn it is.
struct Tick {
    uint32_t steps;              // physics steps this frame
    uint32_t frames;             // play::STATE_FRAMES after adding them
    int32_t clockwise, counter;  // the two wheel slots' flags
    uint32_t clockwise_step, counter_step;
    uint32_t selection;  // TEXT_SELECTION: 5 Select, 6 Menu, else nothing
    int32_t player;
    uint32_t record;  // the player's record (player_record)
    uint32_t mode;    // menu::GAME_MODE
    bool panel_growing;
    uint32_t pause_byte;   // screen::BYTE_DCF: opened from the pause menu
    uint32_t frame_count;  // TEXT_FRAME_COUNT
};

// Where the stroke that went out of bounds was played from, for the "no out of bounds" cheat;
// see `tick_out_of_bounds` for why it cannot simply be read when it is wanted.
uint32_t out_of_bounds_return_x, out_of_bounds_return_y;

uint32_t state() {
    return play_state().state;
}
void set_state(uint32_t s) {
    play_state().state = s;
}
void restart_frames() {
    play_state().state_frames = 0;
}
int32_t sine(uint32_t angle) {
    return static_cast<int32_t>(guest_array<int16_t>(SINE_TABLE)[angle & 0xff]);
}
uint32_t strokes(const Tick& tick) {
    return as_player(tick.record).strokes;
}
uint32_t tile(uint32_t x, uint32_t y) {
    return tile_at(x, y);
}

void panel_grow(uint32_t step) {
    play_state().panel_growing = static_cast<uint8_t>(1);
    play_state().panel_scale = 0;
    play_state().panel_scale_step = step;
}
void panel_shrink() {
    play_state().panel_growing = static_cast<uint8_t>(1);
    play_state().panel_scale_step = PANEL_STEP_OUT;
}

void open_pause_menu(uint32_t remembered_row) {
    text_block().menu_return_row = static_cast<uint8_t>(remembered_row);
    pause_menu_screen_enter();  // pause_menu_enter
}

// Every state that waits on a message panel: Select or Menu sends it away.
void dismiss_on_button(const Tick& tick) {
    if (tick.selection == SELECT || tick.selection == MENU_BUTTON) {
        panel_shrink();
    }
}

// 0x18009c0c — a hole is over: on to the next, or close the course out with its message and
// its record (single player: best round, and the course unlocked by making par).
//
// A round played with a rule-changing cheat leaves no trace: not the best round under the
// course picture, not the statistics, and not the course it would have unlocked (cheats.h). It
// is still played out to the end — the card and the closing message are the same — it simply
// does not count, which is what stops one toggle from quietly rewriting every number the game
// keeps.
void hole_finish() {
    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    const bool counts = !round_records_void();
    const int32_t mode = static_cast<int32_t>(static_cast<int8_t>(menu_state().game_mode));
    if (mode == static_cast<int32_t>(MODE_SINGLE_PLAYER) && counts) {
        screen_state().holes_played = screen_state().holes_played + 1;
    }
    const int32_t hole = static_cast<int8_t>(static_cast<uint32_t>(menu_state().hole) + 1);
    menu_state().hole = static_cast<int8_t>(static_cast<uint8_t>(static_cast<uint32_t>(hole)));
    if (hole != static_cast<int32_t>(HOLES_PER_COURSE)) {
        set_state(NEXT_HOLE);
        restart_frames();
        return;
    }
    menu_state().hole = static_cast<uint8_t>(HOLES_PER_COURSE - 1);
    game_state_block().save_data_byte_5 = static_cast<uint8_t>(0);
    const uint32_t total = text.total_player1;
    if (mode != static_cast<int32_t>(MODE_SINGLE_PLAYER)) {
        const uint32_t other = text.total_player2;
        result_text() = total == other                                               ? TEXT_TIE
                        : static_cast<int32_t>(total) >= static_cast<int32_t>(other) ? TEXT_P2_WINS
                                                                                     : TEXT_P1_WINS;
        set_state(COURSE_MESSAGE);
        return;
    }
    const int32_t course = static_cast<int32_t>(menu_state().course);
    // By address: a reference to a field of a packed overlay is not portable.
    uint32_t& best =
        guest<uint32_t>(SCREEN_OBJECT + static_cast<uint32_t>(offsetof(ScreenState, best_round)) +
                        static_cast<uint32_t>(course) * 4);
    if (counts && static_cast<int32_t>(total) < static_cast<int32_t>(best)) {
        best = total;
    }
    round_finished(static_cast<uint32_t>(course), total);
    const int32_t unlocked =
        static_cast<int32_t>(static_cast<uint32_t>(screen_state().courses_unlocked));
    // A cheated round is told nothing new, on the same path as a course already finished
    // before: no unlock, and no message claiming one.
    if (!counts || course != unlocked - 1) {
        result_text() = 0xffff'ffffu;
    } else if (static_cast<int32_t>(total) > static_cast<int32_t>(text.total_par)) {
        save_record_snapshot();
        result_text() =
            static_cast<uint32_t>(menu_state().course) == 2 ? TEXT_NICE_TRY_LAST : TEXT_NICE_TRY;
        set_state(COURSE_MESSAGE);
        return;
    } else {  // par or better on the newest course unlocks the next
        screen_state().courses_unlocked =
            static_cast<int8_t>(static_cast<uint8_t>(static_cast<uint32_t>(unlocked + 1)));
        result_text() = TEXT_UNLOCKED;
        if (course == 2) {
            screen_state().courses_unlocked = static_cast<uint8_t>(3);
            result_text() = TEXT_ALL_DONE;
        }
        screen_state().course_loading =
            static_cast<uint8_t>(static_cast<uint32_t>(screen_state().courses_unlocked) - 1);
    }
    save_record_snapshot();
    set_state(COURSE_MESSAGE);
}

// Placing (1): the wheel walks the ball along the tee; Select starts aiming, Menu pauses.
void tick_placing(const Tick& tick) {
    const uint32_t x = play_state().ball_x, y = play_state().ball_y;
    const uint32_t xi = x >> 16, yi = y >> 16;
    const uint32_t direction = static_cast<uint32_t>(play_state().place_direction);
    if (tick.counter & 2) {  // up, or right, or both, looking four tiles ahead
        if (direction == 1) {
            if (tile(xi, yi - 4) == TILE_TEE) {
                play_state().ball_y = y - 0x10000;
            }
        } else if (direction == 0) {
            if (tile(xi + 3, yi) == TILE_TEE) {
                play_state().ball_x = x + 0x10000;
            }
        } else if (direction == 2 && tile(xi + 3, yi - 4) == TILE_TEE) {
            play_state().ball_x = x + 0x10000;
            play_state().ball_y = y - 0x10000;
        }
        return;
    }
    if (tick.clockwise & 2) {  // the other way
        if (direction == 1) {
            if (tile(xi, yi + 3) == TILE_TEE) {
                play_state().ball_y = y + 0x10000;
            }
        } else if (direction == 0) {
            if (tile(xi - 4, yi) == TILE_TEE) {
                play_state().ball_x = x - 0x10000;
            }
        } else if (direction == 2 && tile(xi - 4, yi + 3) == TILE_TEE) {
            play_state().ball_x = x - 0x10000;
            play_state().ball_y = y + 0x10000;
        }
        return;
    }
    if (tick.selection == SELECT) {
        set_state(AIMING);
    } else if (tick.selection == MENU_BUTTON) {
        open_pause_menu(0xff);
    }
}

// How far the aim line reaches: walk along the aim until a tile blocks either edge of the
// ball, growing the meter's extent as it goes.
void aim_line_measure() {
    const uint32_t angle = play_state().aim_angle;
    const int32_t dx = sine(angle + QUARTER_TURN) * 2;
    const int32_t dy = -(sine(angle) * 2);
    const int32_t side_x = (-dx) >> 6, side_y = dy >> 6;
    const int32_t x0 = static_cast<int32_t>(play_state().ball_x);
    const int32_t y0 = static_cast<int32_t>(play_state().ball_y);
    int32_t reach = static_cast<int32_t>(play_state().meter_start);
    int32_t width = 0;
    for (; reach < 0x80; reach += static_cast<int32_t>(play_state().meter_speed)) {
        const int32_t x = reach * dx + x0, y = reach * dy + y0;
        const int32_t wx = (side_y * width) >> 10, wy = (side_x * width) >> 10;
        if (point_blocked(static_cast<int32_t>(static_cast<uint32_t>((x + wx) >> 16)),
                          static_cast<int32_t>(static_cast<uint32_t>((y + wy) >> 16))) != 0) {
            break;
        }
        if (point_blocked(static_cast<int32_t>(static_cast<uint32_t>((x - wx) >> 16)),
                          static_cast<int32_t>(static_cast<uint32_t>((y - wy) >> 16))) != 0) {
            break;
        }
        width += static_cast<int32_t>(play_state().meter_step);
        if (static_cast<int32_t>(play_state().meter_max) < width) {
            width = static_cast<int32_t>(play_state().meter_max);
        }
    }
    int32_t value = (reach * static_cast<int32_t>(play_state().meter_scale)) >> 16;
    if (value < 0x1e) {
        value = 0x1e;
    }
    play_state().meter_value = static_cast<uint32_t>(value);
}

// The wheel turns the aim; it speeds up the longer it is turned the same way.
void aim_turn(uint32_t slot_step, uint32_t direction, bool adding) {
    if (static_cast<uint32_t>(play_state().wheel_direction) != direction) {
        play_state().wheel_repeats = 0;
    }
    const uint32_t repeats = play_state().wheel_repeats + 1;
    play_state().wheel_repeats = repeats;
    int32_t index = static_cast<int32_t>(slot_step) - 1;
    const int32_t count = static_cast<int32_t>(guest<uint32_t>(AIM_SPEED_COUNT));
    if (index < 0) {
        index = 0;
    } else if (index >= count) {
        index = count - 1;
    }
    const uint32_t speed = guest_array<uint32_t>(AIM_SPEEDS)[index];
    if (!(speed == 1 && static_cast<int32_t>(repeats) <= 2)) {  // the first two detents are free
        const uint32_t angle = play_state().aim_angle;
        play_state().aim_angle = adding ? angle + speed : angle - speed;
    }
    play_state().wheel_direction = static_cast<uint8_t>(direction);
}

// Aiming (2).
void tick_aiming(const Tick& tick) {
    if (tick.clockwise & 2) {
        aim_turn(tick.clockwise_step, 0, true);
    } else if (tick.counter & 2) {
        aim_turn(tick.counter_step, 1, false);
    } else if (tick.selection == SELECT) {
        set_state(POWER);
        restart_frames();
    } else if (tick.selection == MENU_BUTTON) {
        if (strokes(tick) != 0 && player_record(static_cast<uint32_t>(tick.player)).placed == 0) {
            open_pause_menu(0xff);
        } else {  // undo the last stroke? ask
            set_state(CONFIRM);
            play_state().state_after = AIMING;
            panel_message_show(TEXT_UNDO_AIM);
        }
    }
    aim_line_measure();
}

// Power (3): Select strikes with the meter's reading; Menu asks to go back to aiming.
void tick_power(const Tick& tick) {
    if (tick.selection == MENU_BUTTON) {
        set_state(CONFIRM);
        play_state().state_after = POWER;
        panel_message_show(TEXT_UNDO_POWER);  // a tail call in the original
        return;
    }
    if (tick.selection != SELECT) {
        return;
    }
    set_state(STRUCK);
    const int32_t power = power_meter_value();
    restart_frames();
    const int32_t speed =
        ((static_cast<int32_t>(play_state().meter_gain) * (power >> 8)) >> 7) >> 4;
    const uint32_t angle = play_state().aim_angle;
    play_state().velocity_x =
        static_cast<uint32_t>((speed * ((sine(angle + QUARTER_TURN) * 2) >> 4)) >> 8);
    play_state().velocity_y = static_cast<uint32_t>((speed * ((-(sine(angle) * 2)) >> 4)) >> 8);
    play_state().stroke_start_x = play_state().ball_x;
    play_state().stroke_start_y = play_state().ball_y;
    play_state().byte_796 = static_cast<uint8_t>(0);
    menu_sound_play(SOUND_PUTT);
    as_player(tick.record).strokes = static_cast<uint16_t>(strokes(tick) + 1);
    if (tick.mode == MODE_SINGLE_PLAYER && !round_records_void()) {
        screen_state().statistic_da8 = screen_state().statistic_da8 + 1;  // total strokes
    }
}

// The ball came to rest (or the stroke limit fell): what the next stroke starts from.
void stroke_limit_reached(const Tick& tick) {
    hole_abandoned();  // not a score to remember, and not a path to follow (round_history.h)
    as_player(tick.record).result = static_cast<uint16_t>(2);
    player_record(0)
        .strokes_by_hole[static_cast<uint32_t>(tick.player) * PLAYER_STRIDE +
                         static_cast<uint32_t>(static_cast<uint32_t>(menu_state().hole))] =
        static_cast<int8_t>(stroke_limit());
    set_state(LIMIT_MESSAGE);
}

// After a step: the common tail of the rolling states — record the rest position if the
// ball is back in the player's hands, and enforce the stroke limit.
void after_rolling(const Tick& tick) {
    const uint32_t s = state();
    if (s == PLACING || s == AIMING || s == CARD) {
        ball_rest_record();
        if (static_cast<int32_t>(static_cast<int16_t>(as_player(tick.record).strokes)) >=
            static_cast<int32_t>(stroke_limit())) {
            stroke_limit_reached(tick);
        }
    }
}

bool barely_moved(uint32_t before_x, uint32_t before_y) {
    const uint32_t dx = before_x - play_state().ball_x + 0x400;
    const uint32_t dy = before_y - play_state().ball_y + 0x400;
    return dx <= 0x800 && dy <= 0x800;
}

// Rolling (5): one step a frame; Select skips to the fast-forward state. At rest for ten steps
// the stroke ends.
void tick_rolling(const Tick& tick) {
    if (tick.selection == SELECT) {
        trail_reset();
        set_state(FAST_FORWARD);
        return;
    }
    const uint32_t before_x = play_state().ball_x, before_y = play_state().ball_y;
    ball_step();
    ball_sample();
    const uint32_t x = play_state().ball_x, y = play_state().ball_y;
    const bool moving = !barely_moved(before_x, before_y);
    if (moving ||
        static_cast<int32_t>(play_state().state_frames) <= static_cast<int32_t>(REST_STEPS)) {
        if (moving) {
            restart_frames();
        }
        after_rolling(tick);
        return;
    }
    // At rest: let the tile have its say; if it leaves the ball where it is, the stroke is over.
    play_state().velocity_x = 0;
    play_state().velocity_y = 0;
    ball_move(0xff);
    if (play_state().ball_x != x || play_state().ball_y != y) {
        restart_frames();
        after_rolling(tick);
        return;
    }
    set_state(AIMING);
    play_state().velocity_x = 0;
    play_state().velocity_y = 0;
    const bool first = strokes(tick) == 1;
    text_block().word_734 = first ? 4 : 5;
    text_block().word_738 = first ? 3 : 4;
    text_block().word_73c = 0;
    ball_rest_record();
    if (static_cast<int32_t>(static_cast<int16_t>(as_player(tick.record).strokes)) >=
        static_cast<int32_t>(stroke_limit())) {
        stroke_limit_reached(tick);
    }
}

// Holed (6, 7): the ball slides from where it is into the cup over a few frames.
void tick_holed() {
    set_state(SINKING);
    restart_frames();
    const int32_t dx = static_cast<int32_t>(play_state().sink_to_x - play_state().sink_from_x) >> 8;
    const int32_t dy = static_cast<int32_t>(play_state().sink_to_y - play_state().sink_from_y) >> 8;
    const uint32_t frames = integer_sqrt(static_cast<uint32_t>(dx * dx + dy * dy)) >> 10;
    play_state().sink_frames = frames < 8 ? 8u : frames;
}

void tick_sinking() {
    const int32_t f = static_cast<int32_t>(play_state().state_frames);
    const int32_t n = static_cast<int32_t>(play_state().sink_frames);
    if (f > n) {
        play_state().ball_x = play_state().sink_to_x;
        play_state().ball_y = play_state().sink_to_y;
        set_state(ROLLING);
        restart_frames();
        return;
    }
    const int32_t left = n - f;
    const libc::Division division = libc::signed_divide(
        static_cast<uint32_t>(left * static_cast<int32_t>(play_state().sink_from_x) +
                              f * static_cast<int32_t>(play_state().sink_to_x)),
        static_cast<uint32_t>(n));
    play_state().ball_x = division.quotient;
    const libc::Division centred_y = libc::signed_divide(
        static_cast<uint32_t>(left * static_cast<int32_t>(play_state().sink_from_y) +
                              f * static_cast<int32_t>(play_state().sink_to_y)),
        static_cast<uint32_t>(n));
    play_state().ball_y = centred_y.quotient;
    play_state().hint = static_cast<uint8_t>(4);
}

// The animated objects: each has a kind, a frame, and a saved frame.
uint32_t object_count() {
    return static_cast<uint32_t>(players_state().object_count);
}
bool object_animates(const HoleSprite& object, int32_t& kind) {
    kind = object.kind;
    const bool high = kind >= static_cast<int32_t>(OBJECT_KIND_HIGH);
    const int32_t limit =
        static_cast<int16_t>(high ? players_state().kind_limit_b : players_state().kind_limit_a);
    if (high) {
        kind -= static_cast<int32_t>(OBJECT_KIND_HIGH);
    }
    return kind < limit;
}

// Fast forward (9): up to a hundred steps at once, the objects animating along, the trail
// recorded, until the ball rests (then aiming) or the limit falls.
void tick_fast_forward(const Tick& tick) {
    for (uint32_t i = 0; i < object_count(); ++i) {  // remember the animation frames
        int32_t kind;
        if (object_animates(sprite_at(i), kind)) {
            sprite_at(i).frame_saved = sprite_at(i).frame;
        }
    }
    uint32_t phase = play_state().animation_phase;
    for (int32_t step = 0;
         state() == FAST_FORWARD && step < static_cast<int32_t>(FAST_FORWARD_STEPS); ++step) {
        const uint32_t before_x = play_state().ball_x, before_y = play_state().ball_y;
        ball_step();
        ball_sample();
        const uint32_t dx = before_x - play_state().ball_x;
        const uint32_t dy = before_y - play_state().ball_y;
        phase = phase + 1 == ANIMATION_PHASES ? 0 : phase + 1;
        for (uint32_t i = 0; i < object_count(); ++i) {
            int32_t kind;
            HoleSprite& object = sprite_at(i);
            if (!object_animates(object, kind)) {
                continue;
            }
            const FrameList& frames =
                sprite_frames(object.kind, static_cast<int32_t>(OBJECT_KIND_HIGH));
            if (phase == 0 && state() != POWER) {
                object.frame = static_cast<int16_t>(object.frame + 1);
            }
            if (object.frame >= frames.count) {
                object.frame = 0;
            }
        }
        const uint32_t trail = static_cast<uint32_t>(play_state().trail_index);
        play_state().trail_x[trail] = static_cast<uint16_t>(play_state().ball_x >> 16);
        play_state().trail_y[trail] = static_cast<uint16_t>(play_state().ball_y >> 16);
        play_state().trail_index = static_cast<uint8_t>((trail + 1) & (TRAIL_SIZE - 1));
        play_state().trail_length = play_state().trail_length + 1;
        const uint32_t frames = play_state().state_frames + 1;
        play_state().state_frames = frames;
        if (dx + 0x400 > 0x800 || dy + 0x400 > 0x800) {
            restart_frames();
        } else if (static_cast<int32_t>(frames) > static_cast<int32_t>(REST_STEPS)) {
            set_state(AIMING);
            if (static_cast<int32_t>(static_cast<int16_t>(as_player(tick.record).strokes)) >=
                static_cast<int32_t>(stroke_limit())) {
                stroke_limit_reached(tick);
            }
        }
    }
    for (uint32_t i = 0; i < object_count(); ++i) {  // and put the frames back
        int32_t kind;
        if (object_animates(sprite_at(i), kind)) {
            sprite_at(i).frame = sprite_at(i).frame_saved;
        }
    }
    after_rolling(tick);
}

// The hint sequence (10-13): three hints, two frames each, then back to the saved state. A
// state that has had its two frames hands over within the same tick, so the hint byte is already
// the next one when the frame is drawn.
void tick_hints() {
    if (state() == HINT_START) {
        set_state(HINT_1);
        restart_frames();
    }
    for (;;) {
        const uint32_t s = state();
        play_state().hint = static_cast<uint8_t>(s - HINT_START);
        if (play_state().state_frames < HINT_FRAMES) {
            return;
        }
        if (s == HINT_3) {
            set_state(play_state().saved_state);
            play_state().state_frames = play_state().saved_frames;
            return;
        }
        set_state(s + 1);
        restart_frames();
    }
}

// A message is on the panel: wait for it to be sent away, then for it to go.
bool message_still_up(const Tick& tick) {
    if (tick.panel_growing) {
        return true;
    }
    if (static_cast<int32_t>(play_state().panel_scale_step) >= 0) {
        dismiss_on_button(tick);
        return true;
    }
    return false;
}

// The stroke limit (14, 15) and out of bounds (16, 17).
void tick_limit(const Tick& tick) {
    if (state() == LIMIT_MESSAGE) {
        panel_message_show(TEXT_LIMIT);
        set_state(LIMIT_WAIT);
    }
    if (message_still_up(tick)) {
        return;
    }
    set_state(HOLE_DONE);
    ball_rest_record();
}

void tick_out_of_bounds(const Tick& tick) {
    if (state() == OUT_MESSAGE) {
        // Taken here because the two lines below overwrite play::STROKE_START with the ball's
        // position on every frame the message is up: this is the last frame on which it still
        // says where the stroke was played from.
        out_of_bounds_return_x = play_state().stroke_start_x;
        out_of_bounds_return_y = play_state().stroke_start_y;
        set_state(OUT_WAIT);
        panel_message_show(TEXT_OUT);
    }
    play_state().stroke_start_x = play_state().ball_x;
    play_state().stroke_start_y = play_state().ball_y;
    if (message_still_up(tick)) {
        return;
    }
    if (static_cast<int32_t>(static_cast<int16_t>(as_player(tick.record).strokes)) <
        static_cast<int32_t>(stroke_limit())) {
        if (cheat_enabled(Cheat::NoOutOfBounds)) {
            // The ball is put back where the stroke was played from and the player aims again.
            // The stroke still counts — the swing happened — but leaving the green costs
            // nothing beyond it. A result of 1 is "the hole is in play", which is what makes
            // `ball_rest_record` hand back to aiming rather than to placing on the tee.
            play_state().ball_x = out_of_bounds_return_x;
            play_state().ball_y = out_of_bounds_return_y;
            as_player(tick.record).result = static_cast<uint16_t>(1);
            ball_rest_record();
            return;
        }
        as_player(tick.record).result = static_cast<uint16_t>(0xffff);
        ball_rest_record();
        return;
    }
    stroke_limit_reached(tick);
}

// The other player's turn (18, 19).
void tick_turn(const Tick& tick) {
    if (state() == TURN_MESSAGE) {
        set_state(TURN_WAIT);
        panel_message_show((tick.player != 0 ? TEXT_PLAYER2_TURN : TEXT_PLAYER1_TURN));
    }
    play_state().stroke_start_x = play_state().ball_x;
    play_state().stroke_start_y = play_state().ball_y;
    if (message_still_up(tick)) {
        return;
    }
    turn_resume();
    if (text_block().word_734 == 0 || strokes(tick) != 0) {
        return;
    }
    text_block().word_734 = 2;
    text_block().word_738 = 2;
    text_block().word_73c = 0;
}

// Hole done (20): the sound for the score, the record, and on to the card or the next hole.
void tick_hole_done(const Tick& tick) {
    if (static_cast<int16_t>(as_player(tick.record).strokes) == 1) {
        if (tick.mode == MODE_SINGLE_PLAYER && !round_records_void()) {
            screen_state().holes_in_one = screen_state().holes_in_one + 1;
            screen_state().word_d9c = screen_state().word_d9c + 1;
        }
        menu_sound_play(SOUND_HOLE_IN_ONE);
        if (text_block().word_734 != 6) {
            text_block().word_734 = 6;
            text_block().word_738 = 2;
            text_block().word_73c = 0;
        }
    } else {
        const int32_t course = static_cast<int32_t>(menu_state().course);
        const int32_t hole = static_cast<int32_t>(menu_state().hole);
        const int32_t par = course_info_at(course).pars[hole];
        const int32_t taken =
            static_cast<int32_t>(static_cast<int16_t>(as_player(tick.record).strokes));
        menu_sound_play(par > taken ? SOUND_UNDER_PAR : par == taken ? SOUND_PAR : SOUND_OVER_PAR);
    }
    as_player(tick.record).result = static_cast<uint16_t>(2);
    hole_finished(static_cast<uint32_t>(static_cast<int32_t>(menu_state().course)),
                  static_cast<uint32_t>(static_cast<int32_t>(menu_state().hole)),
                  static_cast<uint32_t>(static_cast<int16_t>(as_player(tick.record).strokes)));
    player_record(0)
        .strokes_by_hole[static_cast<uint32_t>(tick.player) * PLAYER_STRIDE +
                         static_cast<uint32_t>(static_cast<uint32_t>(menu_state().hole))] =
        static_cast<int8_t>(as_player(tick.record).strokes);
    if (tick.mode == MODE_PASS_N_PLAY) {
        ball_rest_record();
    } else if (tick.mode != MODE_PRACTICE_HOLE) {
        text_block().score_card_flag = static_cast<uint8_t>(0);
        score_card_open();  // score_card_open, a tail call in the original
    } else {                // practice: the same hole again
        course_unload();
        course_start(0);  // course_start
    }
}

// The score card's wheel: a row up or down once the wheel has turned far enough.
void card_wheel(const Tick& tick, bool down) {
    const uint32_t step = down ? tick.counter_step : tick.clockwise_step;
    const uint32_t direction = down ? 1 : 0;
    const uint32_t repeat = static_cast<uint32_t>(play_state().wheel_direction) == direction
                                ? play_state().wheel_repeat + step
                                : step;
    play_state().wheel_repeat = repeat;
    play_state().wheel_direction = static_cast<uint8_t>(direction);
    if (static_cast<int32_t>(repeat) <= static_cast<int32_t>(play_state().wheel_repeat_limit)) {
        return;
    }
    play_state().wheel_repeat = 0;
    const int32_t cursor = static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor));
    const int32_t first = static_cast<int32_t>(static_cast<uint32_t>(menu_state().first_row));
    if (!down) {
        const int32_t row = static_cast<int8_t>(cursor - 1);
        text_block().carousel_course =
            static_cast<uint8_t>(row < 0 ? 0u : static_cast<uint32_t>(row));
        if (cursor < first) {
            text_block().score_card_first = static_cast<uint8_t>(static_cast<uint32_t>(cursor));
        }
    } else {
        int32_t row = static_cast<int8_t>(cursor + 1);
        text_block().carousel_course = static_cast<uint8_t>(static_cast<uint32_t>(row));
        const int32_t visible =
            static_cast<int32_t>(static_cast<uint32_t>(menu_state().visible_rows));
        const int32_t count = static_cast<int32_t>(
            static_cast<uint32_t>(static_cast<int8_t>(menu_state().item_count)));
        if (row + visible > count) {
            text_block().carousel_course =
                static_cast<uint8_t>(static_cast<uint32_t>(count - visible));
        }
    }
}

// The score card (21-23): slides in, takes the wheel, slides out.
void tick_card(const Tick& tick) {
    const uint32_t s = state();
    Slide& card = text_block().slides[SCORE_CARD_SLIDE];
    const uint32_t dy = card.speed;
    if (s == CARD_IN) {
        card.y = card.y + dy;
        if (tick.frame_count == SLIDE_FRAMES) {
            set_state(CARD);
            text_block().frame_count = 0;
        }
        return;
    }
    if (s == CARD_OUT) {
        card.y = card.y - 2 * dy;
        if (tick.frame_count != SLIDE_FRAMES) {
            return;
        }
        if (tick.pause_byte == 1) {
            open_pause_menu(3);
        } else {
            hole_finish();  // a tail call in the original
        }
        return;
    }
    // CARD: the wheel scrolls it unless this is practice; a button sends it away.
    if (tick.selection == 0) {
        if (tick.mode == MODE_PRACTICE_HOLE) {
            return;
        }
        card_wheel(tick, false);
        if (static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor)) !=
            static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor))) {
            menu_sound_play(1);
        }
    } else if (tick.selection == 1) {
        if (tick.mode == MODE_PRACTICE_HOLE) {
            return;
        }
        card_wheel(tick, true);
    } else if (tick.selection == SELECT || tick.selection == MENU_BUTTON) {
        set_state(CARD_OUT);
        text_block().frame_count = 0;
        if (tick.pause_byte == 0) {
            text_block().word_734 = 1;
            text_block().word_738 = 1;
            text_block().word_73c = 0;
        }
    }
}

// The end-of-course message (24): the result text, the course name if one was unlocked,
// and the round's statistics; laid out on the panel.
void tick_course_message(const Tick& tick) {
    set_state(COURSE_MESSAGE_WAIT);
    PackRecord& pack = as_pack(game_state_block().pack_handle);
    const uint32_t result = result_text();
    const bool wide = static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
    const auto append = [&](uint32_t destination, uint32_t source) {
        if (wide) {
            wide_string_append(destination, source);
        } else {
            string_append(destination, source);
        }
    };
    const auto& number = wide ? wide_number_to_string : number_to_string;
    if (result == 0xffff'ffffu) {
        guest<uint8_t>(DIALOG_TEXT) = static_cast<uint8_t>(0);
        guest<uint8_t>(DIALOG_TEXT + 1) = static_cast<uint8_t>(0);
    } else {
        resource_load(pack, result, DIALOG_TEXT, 0x800);
        if (result == TEXT_UNLOCKED) {
            const uint32_t unlocked =
                static_cast<uint32_t>(static_cast<uint32_t>(screen_state().courses_unlocked));
            resource_load(pack, (unlocked + TEXT_COURSE_NAME_FIRST), DIALOG_MESSAGE, 0x800);
            append(DIALOG_TEXT, DIALOG_MESSAGE);
        }
    }
    if (tick.mode != MODE_PASS_N_PLAY) {  // holes in one and total strokes
        const uint32_t buffer = registers().r[SP] + (wide ? 0x4 : 0xc);
        if (wide) {
            guest<uint16_t>(buffer) = static_cast<uint16_t>(0);
        } else {
            guest<uint8_t>(buffer) = static_cast<uint8_t>(0);
        }
        resource_load(pack, TEXT_HOLES_IN_ONE, DIALOG_MESSAGE, 0x800);
        append(DIALOG_TEXT, DIALOG_MESSAGE);
        number(buffer, static_cast<int32_t>(screen_state().word_d9c), 0);
        append(DIALOG_TEXT, buffer);
        if (wide) {
            append(DIALOG_TEXT, LITERAL_NEWLINE_WIDE);
            append(DIALOG_TEXT, LITERAL_NEWLINE_WIDE);
        } else {
            append(DIALOG_TEXT, LITERAL_NEWLINE);
        }
        resource_load(pack, TEXT_TOTAL_STROKES, DIALOG_MESSAGE, 0x800);
        append(DIALOG_TEXT, DIALOG_MESSAGE);
        number(buffer, static_cast<int32_t>(text_block().total_player1), 0);
        append(DIALOG_TEXT, buffer);
    }
    const uint32_t lines = text_layout(as_font(screen_state().text_layout), DIALOG_TEXT,
                                       TEXT_LAYOUT_OUT, LAYOUT_WIDTH, LAYOUT_HEIGHT);
    if (lines == 0) {
        assert_trap(0x18003d48u);
    }
    text_block().score_card_first = static_cast<uint8_t>(0);
    text_block().carousel_course = static_cast<uint8_t>(0);
    text_block().carousel_count = static_cast<uint8_t>(lines);
    const uint32_t small_line = as_font(screen_state().font_object).line_height;
    const libc::Division rows = libc::signed_divide(
        240 - small_line - 0x58, as_font(screen_state().text_layout).line_height);
    text_block().score_card_rows_shown = static_cast<uint8_t>(rows.quotient);
    panel_grow(PANEL_STEP_IN);
}

// Waiting on it (25): then leave the course — to the menu, or into the course just unlocked.
void tick_course_message_wait(const Tick& tick) {
    if (message_still_up(tick)) {
        return;
    }
    course_unload();
    if (result_text() != TEXT_UNLOCKED) {
        text_block().menu_return_row = static_cast<uint8_t>(0xff);
        main_menu_screen_enter();  // main_menu_enter, a tail call in the original
        return;
    }
    const int32_t next = static_cast<int8_t>(static_cast<uint32_t>(menu_state().course) + 1);
    text_block().byte_745 = static_cast<uint8_t>(static_cast<uint32_t>(next));
    course_load_request(static_cast<uint32_t>(next), 0);  // a tail call
}

// The new course's picture (26-29): drops in, waits for a button, rises away.
void tick_picture(const Tick& tick) {
    const uint32_t s = state();
    Slide& picture_slide = text_block().slides[COURSE_PICTURE_SLIDE];
    const uint32_t dy = picture_slide.speed;
    if (s == PICTURE_START) {
        set_state(PICTURE_IN);
        const uint32_t picture = COURSE_PICTURES + course_picture::SIZE;
        picture_slide.picture = picture;
        text_block().frame_count = 0;
        picture_slide.x = 0;
        const int32_t height = static_cast<int32_t>(as_course_picture(picture).height);
        picture_slide.y = to_fixed(-height);
        const int64_t numerator = static_cast<int64_t>(height) << 32;
        picture_slide.speed =
            libc::divide64(static_cast<uint32_t>(numerator), static_cast<uint32_t>(numerator >> 32),
                           DROP_FRAMES_FIXED, 0);
        return;
    }
    if (s == PICTURE_IN) {
        picture_slide.y = picture_slide.y + dy;
        if (tick.frame_count == SLIDE_FRAMES) {
            set_state(PICTURE);
            text_block().frame_count = 0;
        }
        return;
    }
    if (s == PICTURE) {
        if (tick.selection == SELECT || tick.selection == MENU_BUTTON) {
            set_state(PICTURE_OUT);
            text_block().frame_count = 0;
        }
        return;
    }
    picture_slide.y = picture_slide.y - 2 * dy;
    if (tick.frame_count == SLIDE_FRAMES) {
        restart_frames();
        ball_rest_record();
    }
}

// The wipe to the next hole (30): the objects swing in over sixty frames; Menu skips ahead.
void tick_next_hole(const Tick& tick) {
    const uint32_t base = hole_objects_state().cursor;
    uint32_t frames = tick.frames;
    if (tick.selection == MENU_BUTTON) {
        frames = 0x3d;
        play_state().state_frames = frames;
        play_state().byte_799 = static_cast<uint8_t>(1);
    } else if (frames >= 0x3f) {
        course_unload();
        course_start(0);  // course_start
        return;
    } else if (frames < WIPE_FRAMES) {
        hole_objects_state().cursor = base + tick.steps * 0x30;
        ground_camera_prepare(1);
    }
    if (frames >= WIPE_FRAMES) {
        const uint32_t gone = frames - tick.steps;
        if (gone >= WIPE_FRAMES) {
            return;
        }
        hole_objects_state().cursor = base + (WIPE_FRAMES - gone) * 0x30;
        ground_camera_prepare(1);
    }
    // Every object: its rotation about the wipe's angle.
    const bool late = frames >= WIPE_FRAMES;
    for (uint32_t i = 0; i < hole_objects_state().count; ++i) {
        HoleObject& object =
            as_object(hole_objects_state().table + hole_objects::OBJECT_WORDS * 4 * i);
        if (object.kind >= 0x46) {
            assert_trap(late ? 0x18003f60u : 0x18004000u);
        }
        const uint32_t angle = to_fixed(object.angle);
        const uint32_t s = static_cast<uint32_t>(sine_degrees(static_cast<int32_t>(0u - angle)));
        const uint32_t c =
            static_cast<uint32_t>(sine_degrees(static_cast<int32_t>(0x5a0000u - angle)));
        // An index loop, not std::fill or a range-for: both want a pointer or a reference into
        // a packed array, which a compiler may refuse (aarch64-none-elf-g++ does).
        for (size_t element = 0; element < std::size(object.matrix); ++element) {
            object.matrix[element] = 0u;
        }
        object.matrix[4] = s;
        object.matrix[1] = 0u - s;
        object.matrix[0] = c;
        object.matrix[10] = 0x10000;
        object.matrix[5] = c;
        object.matrix[15] = 0x10000;
    }
}

// The confirmation (31): the panel counts down unless a button answers.
void tick_confirm(const Tick& tick) {
    if (tick.panel_growing) {
        play_state().confirm_countdown = WIPE_FRAMES;
        return;
    }
    if (play_state().panel_scale == 0) {
        set_state(play_state().state_after);
    } else {
        play_state().confirm_countdown = play_state().confirm_countdown - 1;
        if (tick.selection == MENU_BUTTON || tick.selection == SELECT) {
            play_state().confirm_countdown = 0;
            return;
        }
    }
    if (static_cast<int32_t>(play_state().confirm_countdown) < 0) {
        panel_shrink();
    }
}

}  // namespace

// 0x18002c28 — one frame of the hole, `frame_ms` long.
void hole_tick(uint32_t frame_ms) {
    play_state().ticked = static_cast<uint8_t>(1);
    play_state().frame_ms = frame_ms;
    play_state().elapsed_ms = play_state().elapsed_ms + frame_ms;
    const libc::Division steps =
        libc::signed_divide(play_state().step_remainder + frame_ms, MS_PER_STEP);
    Tick tick;
    tick.steps = steps.quotient;
    play_state().steps_this_frame = tick.steps;
    tick.frames = play_state().state_frames + tick.steps;
    play_state().state_frames = tick.frames;
    play_state().step_remainder = steps.remainder;
    play_state().hint = static_cast<uint8_t>(0);
    tick.clockwise = static_cast<int16_t>(wheel_slot_at(WHEEL_CLOCKWISE).flags);
    tick.counter = static_cast<int16_t>(wheel_slot_at(WHEEL_COUNTER).flags);
    tick.clockwise_step = wheel_slot_at(WHEEL_CLOCKWISE).step;
    tick.counter_step = wheel_slot_at(WHEEL_COUNTER).step;
    tick.player = static_cast<int32_t>(static_cast<uint32_t>(players_state().current));
    tick.record =
        MENU + player::STROKES_BY_HOLE + static_cast<uint32_t>(tick.player) * PLAYER_STRIDE;
    tick.mode =
        static_cast<uint32_t>(static_cast<uint32_t>(static_cast<int8_t>(menu_state().game_mode)));
    tick.panel_growing = static_cast<uint32_t>(play_state().panel_growing) != 0;
    tick.pause_byte = static_cast<uint32_t>(screen_state().byte_dcf);
    tick.frame_count = text_block().frame_count;
    tick.selection = static_cast<uint32_t>(text_block().selection);

    switch (state()) {
    case PLACING:
        tick_placing(tick);
        break;
    case AIMING:
        tick_aiming(tick);
        break;
    case POWER:
        tick_power(tick);
        break;
    case STRUCK:
        if (static_cast<int32_t>(tick.frames) < static_cast<int32_t>(STRIKE_DELAY_STEPS)) {
            break;
        }
        set_state(ROLLING);
        restart_frames();
        player_record(static_cast<uint32_t>(tick.player)).placed = 0;
        tick_rolling(tick);
        break;
    case ROLLING:
        tick_rolling(tick);
        break;
    case HOLED:
    case HOLED_ALT:
        tick_holed();
        tick_sinking();
        break;
    case SINKING:
        tick_sinking();
        break;
    case FAST_FORWARD:
        tick_fast_forward(tick);
        break;
    case HINT_START:
    case HINT_1:
    case HINT_2:
    case HINT_3:
        tick_hints();
        break;
    case LIMIT_MESSAGE:
    case LIMIT_WAIT:
        tick_limit(tick);
        break;
    case OUT_MESSAGE:
    case OUT_WAIT:
        tick_out_of_bounds(tick);
        break;
    case TURN_MESSAGE:
    case TURN_WAIT:
        tick_turn(tick);
        break;
    case HOLE_DONE:
        tick_hole_done(tick);
        break;
    case CARD_IN:
    case CARD:
    case CARD_OUT:
        tick_card(tick);
        break;
    case COURSE_MESSAGE:
        tick_course_message(tick);
        break;
    case COURSE_MESSAGE_WAIT:
        tick_course_message_wait(tick);
        break;
    case PICTURE_START:
    case PICTURE_IN:
    case PICTURE:
    case PICTURE_OUT:
        tick_picture(tick);
        break;
    case NEXT_HOLE:
        tick_next_hole(tick);
        break;
    case CONFIRM:
        tick_confirm(tick);
        break;
    default:
        break;
    }
}

}  // namespace minigolf::game
