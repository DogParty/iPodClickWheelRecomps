// The game's state as structures overlaid on guest memory (guest.h): one per block the
// original kept, in the original's layout. Generated from the offsets in game_state.h and the
// accesses in the code, then edited by hand.
//
// Over 600 lines on purpose: it is one structure per block of the original's state, and the
// layout is what makes them right. Splitting it would put the static_asserts that pin the
// layout in one file and the fields they pin in another.
//
// TODO: fields still called `byte_NNN`, `word_NNN` or `pointer_NNN` are ones whose meaning is
// not yet known — the number is the offset, so the name says exactly where it is and admits
// that is all we have. There are 59 of them, and about a dozen carry real logic:
// `PlayState::word_638` (the hole state saved across a pause), `word_644` (the ball's spin
// accumulator), `word_7c0`/`word_7c4`, `byte_7c8`, `TextBlock::byte_72d`, `word_734`..`word_73c`,
// `MenuState::word_b0`, `ScreenState::word_d9c`, `App2State::word_08`.
//
// To name one: grep for it, and read every site together — a field written in one state and
// read in another usually names itself from the pair. `--trace-entry=ADDR` and `MINIGOLF_WATCH`
// (runtime/memory.h) show who writes it and when, and `analysis/ghidra/decomp.c` has the
// original's code around the same offset. Rename it here and the compiler finds every use.
#pragma once

#include "game_state.h"
#include "guest.h"

namespace minigolf::game {

struct [[gnu::packed]] PlayState {
    uint8_t pad_0[0x544];
    uint32_t meter_gain;
    uint32_t friction;
    uint32_t restitution;
    uint32_t slope_gentle;
    uint32_t slope_steep;
    uint32_t hole_constants[23];
    uint32_t animation_phase;  // 0..5, advances each step
    uint8_t pad_5b8[0x18];
    uint32_t tee_points;  // -> 4-byte points: x, y halfwords
    uint32_t tee_edges;   // -> 12-byte edges: point a, b halfwords; kinds at +8, +9
    uint32_t word_5d8;
    uint32_t objects;         // -> 12-byte animated objects
    uint32_t obstacle_table;  // -> 8 bytes: x, y halfwords; kind word
    uint32_t surface_table;   // -> 16 bytes: dx, dy halfwords; vx, vy; flags
    uint32_t word_5e8;
    uint32_t wall_table;       // -> 16 bytes: x0, y0, x1, y1 halfwords; normal x, y
    uint32_t peg_table;        // -> 12 bytes: x, y halfwords; normal x, y
    uint32_t frame_counts_a;   // -> frames per object kind
    uint32_t frame_counts_b;   // -> frames per object kind
    uint32_t sprite_sheets;    // -> SpriteSheet table (records.h)
    uint32_t sprite_sheets_b;  // -> a second such table
    uint32_t meter_value;
    uint32_t meter_scale;
    uint32_t meter_max;
    uint32_t meter_step;
    uint32_t meter_start;  // where the aim line's dots begin, in tile units
    uint32_t meter_speed;  // and how far apart they are
    uint32_t word_61c;
    uint32_t word_620;
    uint32_t word_624;
    uint32_t state;        // HoleState
    uint32_t state_after;  // where a confirmation returns to
    uint8_t byte_630;
    uint8_t pad_631[0x3];
    uint32_t confirm_countdown;
    uint32_t word_638;        // ...here while paused; -1 when not paused (inferred)
    uint32_t stroke_start_x;  // 16.16, for undo
    uint32_t stroke_start_y;  // 16.16, for undo
    uint32_t word_644;        // cleared when the ball is placed
    uint32_t velocity_x;      // 16.16 per step
    uint32_t velocity_y;      // 16.16 per step
    uint32_t ball_x;          // 16.16 in tile units
    uint32_t ball_y;          // 16.16 in tile units
    uint32_t aim_angle;       // 0..255 around the circle
    uint32_t state_frames;    // physics steps since the state began
    uint32_t step_remainder;  // milliseconds not yet turned into steps
    uint32_t steps_this_frame;
    uint32_t saved_frames;  // across the hint sequence
    uint32_t saved_state;   // across the hint sequence
    uint32_t sink_from_x;
    uint32_t sink_from_y;
    uint32_t sink_to_x;
    uint32_t sink_to_y;
    uint32_t sink_frames;
    uint8_t pad_684[0x4];
    uint32_t elapsed_ms;
    uint32_t frame_ms;
    uint32_t trail_length;
    uint16_t trail_x[64];  // the ball's trail, a point per step
    uint16_t trail_y[64];
    uint8_t trail_index;  // byte, 0..63
    uint8_t hint;         // byte: which hint the render shows (0 none, 1..4)
    uint8_t byte_796;
    uint8_t place_direction;
    uint8_t ball_arrived;  // byte: the ball reached a cup or surface target
    uint8_t byte_799;
    uint8_t byte_79a;
    uint8_t pad_79b[0x23];
    uint8_t byte_7be;
    uint8_t pad_7bf[0x1];
    uint32_t word_7c0;
    uint32_t word_7c4;
    uint8_t byte_7c8;  // score entries loaded so far
    uint8_t pad_7c9[0x3];
    uint32_t file_handle;  // the save file while open, else -1
    uint32_t pointer_7d0;
    uint32_t slot;  // slot index from slot_allocate (inferred)
    uint32_t word_7d8;
    uint8_t sound_enabled[10];  // a byte per menu sound, from the sound table
    uint8_t pad_7e6[0x2];
    uint32_t word_7e8;
    uint32_t music_level;  // Audio #51 scaled to 0..100 by Audio #52
    uint8_t pad_7f0[0x4];
    uint32_t device_level;    // miscTBD #6 rounded down to a multiple of 10
    uint8_t audio_flag;       // byte: Audio #56 == 1
    uint8_t wheel_direction;  // byte: which way the wheel last turned
    uint8_t pad_7fa[0x2];
    uint32_t wheel_repeat;
    uint32_t wheel_repeat_limit;
    uint32_t wheel_repeats;
    uint32_t word_808;  // 4 at start-up
    uint8_t panel_growing;
    uint8_t pad_80d[0x3];
    uint32_t panel_scale;
    uint32_t panel_scale_step;
    uint8_t panel_pressed;  // byte: the button (5 Select, 6 Menu) that closed the panel
    uint8_t byte_819;
    uint8_t byte_81a;
    uint8_t resuming;  // byte: the course was resumed from a save
    uint8_t ticked;    // byte: the hole tick ran this frame
};
static_assert(offsetof(PlayState, meter_gain) == 0x544);
static_assert(offsetof(PlayState, friction) == 0x548);
static_assert(offsetof(PlayState, restitution) == 0x54c);
static_assert(offsetof(PlayState, slope_gentle) == 0x550);
static_assert(offsetof(PlayState, slope_steep) == 0x554);
static_assert(offsetof(PlayState, hole_constants) == 0x558);
static_assert(offsetof(PlayState, animation_phase) == 0x5b4);
static_assert(offsetof(PlayState, tee_points) == 0x5d0);
static_assert(offsetof(PlayState, tee_edges) == 0x5d4);
static_assert(offsetof(PlayState, word_5d8) == 0x5d8);
static_assert(offsetof(PlayState, objects) == 0x5dc);
static_assert(offsetof(PlayState, obstacle_table) == 0x5e0);
static_assert(offsetof(PlayState, surface_table) == 0x5e4);
static_assert(offsetof(PlayState, word_5e8) == 0x5e8);
static_assert(offsetof(PlayState, wall_table) == 0x5ec);
static_assert(offsetof(PlayState, peg_table) == 0x5f0);
static_assert(offsetof(PlayState, frame_counts_a) == 0x5f4);
static_assert(offsetof(PlayState, frame_counts_b) == 0x5f8);
static_assert(offsetof(PlayState, sprite_sheets) == 0x5fc);
static_assert(offsetof(PlayState, sprite_sheets_b) == 0x600);
static_assert(offsetof(PlayState, meter_value) == 0x604);
static_assert(offsetof(PlayState, meter_scale) == 0x608);
static_assert(offsetof(PlayState, meter_max) == 0x60c);
static_assert(offsetof(PlayState, meter_step) == 0x610);
static_assert(offsetof(PlayState, meter_start) == 0x614);
static_assert(offsetof(PlayState, meter_speed) == 0x618);
static_assert(offsetof(PlayState, word_61c) == 0x61c);
static_assert(offsetof(PlayState, word_620) == 0x620);
static_assert(offsetof(PlayState, word_624) == 0x624);
static_assert(offsetof(PlayState, state) == 0x628);
static_assert(offsetof(PlayState, state_after) == 0x62c);
static_assert(offsetof(PlayState, byte_630) == 0x630);
static_assert(offsetof(PlayState, confirm_countdown) == 0x634);
static_assert(offsetof(PlayState, word_638) == 0x638);
static_assert(offsetof(PlayState, stroke_start_x) == 0x63c);
static_assert(offsetof(PlayState, stroke_start_y) == 0x640);
static_assert(offsetof(PlayState, word_644) == 0x644);
static_assert(offsetof(PlayState, velocity_x) == 0x648);
static_assert(offsetof(PlayState, velocity_y) == 0x64c);
static_assert(offsetof(PlayState, ball_x) == 0x650);
static_assert(offsetof(PlayState, ball_y) == 0x654);
static_assert(offsetof(PlayState, aim_angle) == 0x658);
static_assert(offsetof(PlayState, state_frames) == 0x65c);
static_assert(offsetof(PlayState, step_remainder) == 0x660);
static_assert(offsetof(PlayState, steps_this_frame) == 0x664);
static_assert(offsetof(PlayState, saved_frames) == 0x668);
static_assert(offsetof(PlayState, saved_state) == 0x66c);
static_assert(offsetof(PlayState, sink_from_x) == 0x670);
static_assert(offsetof(PlayState, sink_from_y) == 0x674);
static_assert(offsetof(PlayState, sink_to_x) == 0x678);
static_assert(offsetof(PlayState, sink_to_y) == 0x67c);
static_assert(offsetof(PlayState, sink_frames) == 0x680);
static_assert(offsetof(PlayState, elapsed_ms) == 0x688);
static_assert(offsetof(PlayState, frame_ms) == 0x68c);
static_assert(offsetof(PlayState, trail_length) == 0x690);
static_assert(offsetof(PlayState, trail_x) == 0x694);
static_assert(offsetof(PlayState, trail_y) == 0x714);
static_assert(offsetof(PlayState, trail_index) == 0x794);
static_assert(offsetof(PlayState, hint) == 0x795);
static_assert(offsetof(PlayState, byte_796) == 0x796);
static_assert(offsetof(PlayState, place_direction) == 0x797);
static_assert(offsetof(PlayState, ball_arrived) == 0x798);
static_assert(offsetof(PlayState, byte_799) == 0x799);
static_assert(offsetof(PlayState, byte_79a) == 0x79a);
static_assert(offsetof(PlayState, byte_7be) == 0x7be);
static_assert(offsetof(PlayState, word_7c0) == 0x7c0);
static_assert(offsetof(PlayState, word_7c4) == 0x7c4);
static_assert(offsetof(PlayState, byte_7c8) == 0x7c8);
static_assert(offsetof(PlayState, file_handle) == 0x7cc);
static_assert(offsetof(PlayState, pointer_7d0) == 0x7d0);
static_assert(offsetof(PlayState, slot) == 0x7d4);
static_assert(offsetof(PlayState, word_7d8) == 0x7d8);
static_assert(offsetof(PlayState, sound_enabled) == 0x7dc);
static_assert(offsetof(PlayState, word_7e8) == 0x7e8);
static_assert(offsetof(PlayState, music_level) == 0x7ec);
static_assert(offsetof(PlayState, device_level) == 0x7f4);
static_assert(offsetof(PlayState, audio_flag) == 0x7f8);
static_assert(offsetof(PlayState, wheel_direction) == 0x7f9);
static_assert(offsetof(PlayState, wheel_repeat) == 0x7fc);
static_assert(offsetof(PlayState, wheel_repeat_limit) == 0x800);
static_assert(offsetof(PlayState, wheel_repeats) == 0x804);
static_assert(offsetof(PlayState, word_808) == 0x808);
static_assert(offsetof(PlayState, panel_growing) == 0x80c);
static_assert(offsetof(PlayState, panel_scale) == 0x810);
static_assert(offsetof(PlayState, panel_scale_step) == 0x814);
static_assert(offsetof(PlayState, panel_pressed) == 0x818);
static_assert(offsetof(PlayState, byte_819) == 0x819);
static_assert(offsetof(PlayState, byte_81a) == 0x81a);
static_assert(offsetof(PlayState, resuming) == 0x81b);
static_assert(offsetof(PlayState, ticked) == 0x81c);
static_assert(sizeof(PlayState) == 0x81d);
inline PlayState& play_state() {
    return guest<PlayState>(PLAY);
}

struct [[gnu::packed]] ScreenState {
    uint8_t pad_0[0xbac];
    uint32_t font_object;  // from object_create at start-up
    uint32_t text_layout;  // text layout object (+0x4c = line height)
    uint32_t small_font;   // the 10-pixel font (the clock's)
    uint32_t handler;      // (event) -> 0/1; event 5 = Select, 6 = Menu
    uint32_t tick;         // (milliseconds)
    uint32_t render;
    uint32_t next_enter;    // enter routine to run when the screen has slid out
    uint32_t render_saved;  // the dialog keeps the render it covers
    uint32_t menu_items;    // -> table of menu_item (the tables are host state now)
    uint8_t pad_bd0[0x1b4];
    uint8_t save_fresh;
    uint8_t byte_d85;  // bytes: the save was just reset; resuming
    uint8_t pad_d86[0x13];
    int8_t courses_unlocked;  // signed byte: 1..3
    uint8_t pad_d9a[0x2];
    uint32_t word_d9c;
    uint32_t holes_played;   // words: single-player statistics
    uint32_t holes_in_one;   // words: single-player statistics
    uint32_t statistic_da8;  // word, cleared with the statistics
    uint32_t best_round[3];  // word per course; NO_BEST_ROUND when none (inferred)
    uint8_t course_loading;  // byte: the course being loaded
    uint8_t pad_db9[0x3];
    uint32_t saved_ball_x;
    uint32_t saved_ball_y;
    uint8_t pad_dc4[0x4];
    uint32_t saved_state;  // the hole's state for a resume
    uint8_t previous_id;   // byte (SCREEN + 0xcc)
    uint8_t id;            // byte (SCREEN + 0xcd)
    uint8_t phase;         // byte (SCREEN + 0xce)
    uint8_t byte_dcf;
    uint8_t byte_dd0;  // 1 with SAVE_DATA_BYTE_5 = a course to resume (inferred)
    uint8_t pad_dd1[0x23];
    int8_t byte_df4;  // signed; 10 means "no name yet" (inferred)
};
static_assert(offsetof(ScreenState, font_object) == 0xbac);
static_assert(offsetof(ScreenState, text_layout) == 0xbb0);
static_assert(offsetof(ScreenState, small_font) == 0xbb4);
static_assert(offsetof(ScreenState, handler) == 0xbb8);
static_assert(offsetof(ScreenState, tick) == 0xbbc);
static_assert(offsetof(ScreenState, render) == 0xbc0);
static_assert(offsetof(ScreenState, next_enter) == 0xbc4);
static_assert(offsetof(ScreenState, render_saved) == 0xbc8);
static_assert(offsetof(ScreenState, menu_items) == 0xbcc);
static_assert(offsetof(ScreenState, save_fresh) == 0xd84);
static_assert(offsetof(ScreenState, byte_d85) == 0xd85);
static_assert(offsetof(ScreenState, courses_unlocked) == 0xd99);
static_assert(offsetof(ScreenState, word_d9c) == 0xd9c);
static_assert(offsetof(ScreenState, holes_played) == 0xda0);
static_assert(offsetof(ScreenState, holes_in_one) == 0xda4);
static_assert(offsetof(ScreenState, statistic_da8) == 0xda8);
static_assert(offsetof(ScreenState, best_round) == 0xdac);
static_assert(offsetof(ScreenState, course_loading) == 0xdb8);
static_assert(offsetof(ScreenState, saved_ball_x) == 0xdbc);
static_assert(offsetof(ScreenState, saved_ball_y) == 0xdc0);
static_assert(offsetof(ScreenState, saved_state) == 0xdc8);
static_assert(offsetof(ScreenState, previous_id) == 0xdcc);
static_assert(offsetof(ScreenState, id) == 0xdcd);
static_assert(offsetof(ScreenState, phase) == 0xdce);
static_assert(offsetof(ScreenState, byte_dcf) == 0xdcf);
static_assert(offsetof(ScreenState, byte_dd0) == 0xdd0);
static_assert(offsetof(ScreenState, byte_df4) == 0xdf4);
static_assert(sizeof(ScreenState) == 0xdf5);
inline ScreenState& screen_state() {
    return guest<ScreenState>(SCREEN_OBJECT);
}

struct [[gnu::packed]] MenuState {
    uint8_t pad_0[0x20];
    uint32_t title_text;  // the menu's title text id (= TEXT + 0x720); -1 none
    uint8_t item_count;   // byte; on name entry: glyphs on the wheel
    int8_t visible_rows;  // signed byte
    int8_t first_row;     // signed byte: first item shown
    int8_t cursor;        // signed byte: selected item / glyph
    int8_t byte_28;
    int8_t byte_29;
    int8_t name_length;  // signed byte: letters entered so far
    uint8_t byte_2b;
    uint8_t pad_2c[0x2];
    uint8_t byte_2e;
    uint8_t pad_2f[0x1];
    uint8_t game_mode;  // byte: 0 single player, 1 pass 'n play, 2 practice (= TEXT + 0x730)
    uint8_t pad_31[0xf];
    uint8_t page;  // byte: which page ENTER_PAGE shows first (= TEXT + 0x740)
    uint8_t dialog_type;
    uint8_t pad_42[0x1];
    int8_t ball_frame;  // signed byte: which of the three ball frames shows
    uint8_t pad_44[0x1];
    int8_t course;  // signed byte: -1 none, 0, 1, 2
    int8_t hole;    // signed byte: the hole being played, 0-based
    uint8_t pad_47[0x9];
    int8_t language;
    uint8_t pad_51[0xf];
    uint8_t strokes_player1[20];  // a signed byte per hole
    uint16_t player_gender;       // halfword: 0 female, 1 male, for this round
    uint8_t pad_76[0x12];
    uint8_t strokes_player2[40];  // a signed byte per hole
    uint32_t word_b0;             // -1 when a course starts
};
static_assert(offsetof(MenuState, title_text) == 0x20);
static_assert(offsetof(MenuState, item_count) == 0x24);
static_assert(offsetof(MenuState, visible_rows) == 0x25);
static_assert(offsetof(MenuState, first_row) == 0x26);
static_assert(offsetof(MenuState, cursor) == 0x27);
static_assert(offsetof(MenuState, byte_28) == 0x28);
static_assert(offsetof(MenuState, byte_29) == 0x29);
static_assert(offsetof(MenuState, name_length) == 0x2a);
static_assert(offsetof(MenuState, byte_2b) == 0x2b);
static_assert(offsetof(MenuState, byte_2e) == 0x2e);
static_assert(offsetof(MenuState, game_mode) == 0x30);
static_assert(offsetof(MenuState, page) == 0x40);
static_assert(offsetof(MenuState, dialog_type) == 0x41);
static_assert(offsetof(MenuState, ball_frame) == 0x43);
static_assert(offsetof(MenuState, course) == 0x45);
static_assert(offsetof(MenuState, hole) == 0x46);
static_assert(offsetof(MenuState, language) == 0x50);
static_assert(offsetof(MenuState, strokes_player1) == 0x60);
static_assert(offsetof(MenuState, player_gender) == 0x74);
static_assert(offsetof(MenuState, strokes_player2) == 0x88);
static_assert(offsetof(MenuState, word_b0) == 0xb0);
static_assert(sizeof(MenuState) == 0xb4);
inline MenuState& menu_state() {
    return guest<MenuState>(MENU);
}

struct [[gnu::packed]] PlayersState {
    uint8_t pad_0[0x70];
    uint16_t hole_words_low[16];
    uint16_t hole_words_high[20];  // 16 halfwords each: the hole data's words at +0x28, split
    uint16_t tee_point_count;      // halfwords
    uint16_t tee_edge_count;
    uint16_t section_2_count;  // halfwords
    uint16_t object_count;     // halfword
    uint16_t obstacle_count;
    uint16_t wall_count;
    uint16_t peg_count;
    uint16_t kind_limit_a;  // halfwords
    uint16_t sprite_sheet_count;
    uint16_t kind_limit_b;
    uint16_t sprite_sheet_b_count;
    uint16_t current;  // halfword: 0 or 1
};
static_assert(offsetof(PlayersState, hole_words_low) == 0x70);   // players::HOLE_WORDS_LOW
static_assert(offsetof(PlayersState, hole_words_high) == 0x90);  // players::HOLE_WORDS_HIGH
static_assert(offsetof(PlayersState, tee_point_count) == 0xb8);  // players::TEE_POINT_COUNT
static_assert(offsetof(PlayersState, tee_edge_count) == 0xba);   // players::TEE_EDGE_COUNT
static_assert(offsetof(PlayersState, section_2_count) == 0xbc);  // players::SECTION_2_COUNT
static_assert(offsetof(PlayersState, object_count) == 0xbe);     // players::OBJECT_COUNT
static_assert(offsetof(PlayersState, obstacle_count) == 0xc0);   // players::OBSTACLE_COUNT
static_assert(offsetof(PlayersState, wall_count) == 0xc2);       // players::WALL_COUNT
static_assert(offsetof(PlayersState, peg_count) == 0xc4);        // players::PEG_COUNT
static_assert(offsetof(PlayersState, kind_limit_a) == 0xc6);
static_assert(offsetof(PlayersState, sprite_sheet_count) == 0xc8);
static_assert(offsetof(PlayersState, kind_limit_b) == 0xca);
static_assert(offsetof(PlayersState, sprite_sheet_b_count) == 0xcc);  // players::KIND_LIMIT_A
static_assert(offsetof(PlayersState, current) == 0xce);               // players::CURRENT
inline PlayersState& players_state() {
    return guest<PlayersState>(PLAYERS);
}

struct [[gnu::packed]] OptionsState {
    uint8_t pad_0[0x8];
    int8_t music;           // signed byte: 0 off, 1 on, 2 auto
    uint8_t sound_fx;       // byte: 0 off, 1 on
    int8_t player_gender;   // signed byte: 0 random, 1 female, 2 male
    uint8_t clock_battery;  // byte: show the clock and battery while playing
};
static_assert(offsetof(OptionsState, music) == 0x8);          // options::MUSIC
static_assert(offsetof(OptionsState, sound_fx) == 0x9);       // options::SOUND_FX
static_assert(offsetof(OptionsState, player_gender) == 0xa);  // options::PLAYER_GENDER
static_assert(offsetof(OptionsState, clock_battery) == 0xb);  // options::CLOCK_BATTERY
inline OptionsState& options_state() {
    return guest<OptionsState>(OPTIONS);
}

struct [[gnu::packed]] InputState {
    uint8_t hold_state;  // byte: 0 none, 1 Menu held long, 2 Next held long
    uint8_t pad_1[0x3];
    uint32_t menu_hold_limit;     // microseconds before a held Menu counts as long
    uint32_t next_hold_limit;     // likewise for Next
    uint32_t release_next_frame;  // buttons pressed and released within one frame:
    uint32_t ring_index;          // next slot in the 16-entry contact ring
    uint32_t flags;               // the button-flags word (0x18037a0c): see below
    uint32_t menu_press_time;     // clock when Menu went down (inferred)
    uint32_t next_press_time;     // clock when Next went down (inferred)
    uint32_t last_clock;
};
static_assert(offsetof(InputState, hold_state) == 0x0);          // input::HOLD_STATE
static_assert(offsetof(InputState, menu_hold_limit) == 0x4);     // input::MENU_HOLD_LIMIT
static_assert(offsetof(InputState, next_hold_limit) == 0x8);     // input::NEXT_HOLD_LIMIT
static_assert(offsetof(InputState, release_next_frame) == 0xc);  // input::RELEASE_NEXT_FRAME
static_assert(offsetof(InputState, ring_index) == 0x10);         // input::RING_INDEX
static_assert(offsetof(InputState, flags) == 0x14);              // input::FLAGS
static_assert(offsetof(InputState, menu_press_time) == 0x18);    // input::MENU_PRESS_TIME
static_assert(offsetof(InputState, next_press_time) == 0x1c);    // input::NEXT_PRESS_TIME
static_assert(offsetof(InputState, last_clock) == 0x20);         // input::LAST_CLOCK
inline InputState& input_state() {
    return guest<InputState>(INPUT_STATE);
}

struct [[gnu::packed]] AppState {
    uint8_t state;           // byte; non-zero once the game proper is running (inferred)
    uint8_t firmware_state;  // byte; copy of the context's state for this tick
    uint8_t pad_2[0x2];
    uint32_t context;  // the firmware context of the current frame
    uint32_t answer;
    uint32_t mode;          // 2 = a mode that starts the render pipeline (inferred)
    uint32_t wheel_speed;   // accumulated fast wheel travel, for acceleration
    uint32_t frame_count;   // frames since start: 0 fades the screen in, 1 clears it
    uint32_t pipeline_arg;  // second argument to OpenGLES #158 (inferred)
    uint32_t fade;          // float 0..1, the intro fade
    uint32_t frame_delta;   // microseconds since the previous tick
    uint32_t last_tick_clock;
    uint32_t ms_remainder;         // microseconds not yet counted as a whole millisecond
    uint32_t last_wheel_position;  // -1 while the wheel is untouched
};
static_assert(offsetof(AppState, state) == 0x0);                 // app::STATE
static_assert(offsetof(AppState, firmware_state) == 0x1);        // app::FIRMWARE_STATE
static_assert(offsetof(AppState, context) == 0x4);               // app::CONTEXT
static_assert(offsetof(AppState, answer) == 0x8);                // app::ANSWER
static_assert(offsetof(AppState, mode) == 0xc);                  // app::MODE
static_assert(offsetof(AppState, wheel_speed) == 0x10);          // app::WHEEL_SPEED
static_assert(offsetof(AppState, frame_count) == 0x14);          // app::FRAME_COUNT
static_assert(offsetof(AppState, pipeline_arg) == 0x18);         // app::PIPELINE_ARG
static_assert(offsetof(AppState, fade) == 0x1c);                 // app::FADE
static_assert(offsetof(AppState, frame_delta) == 0x20);          // app::FRAME_DELTA
static_assert(offsetof(AppState, last_tick_clock) == 0x24);      // app::LAST_TICK_CLOCK
static_assert(offsetof(AppState, ms_remainder) == 0x28);         // app::MS_REMAINDER
static_assert(offsetof(AppState, last_wheel_position) == 0x2c);  // app::LAST_WHEEL_POSITION
inline AppState& app_state() {
    return guest<AppState>(APP);
}

struct [[gnu::packed]] App2State {
    uint8_t title_loaded;  // byte
    uint8_t pad_1[0x1];
    uint8_t exiting;       // byte; the state machine answers 3 when set
    uint8_t phase;         // byte 0..9: which screen the state machine runs
    uint32_t file_status;  // bytes read or written by the last save operation
    uint32_t word_08;
    uint32_t score_entry;  // index of the score entry being loaded in phase 6
};
static_assert(offsetof(App2State, title_loaded) == 0x0);  // app2::TITLE_LOADED
static_assert(offsetof(App2State, exiting) == 0x2);       // app2::EXITING
static_assert(offsetof(App2State, phase) == 0x3);         // app2::PHASE
static_assert(offsetof(App2State, file_status) == 0x4);   // app2::FILE_STATUS
static_assert(offsetof(App2State, word_08) == 0x8);       // app2::WORD_08
static_assert(offsetof(App2State, score_entry) == 0xc);   // app2::SCORE_ENTRY
inline App2State& app2_state() {
    return guest<App2State>(APP2);
}

struct [[gnu::packed]] HoleObjects {
    uint8_t pad_0[0x838];
    uint32_t count;   // objects in the table
    uint32_t bytes;   // count * OBJECT_SIZE
    uint32_t cursor;  // the layout entry being loaded
    uint32_t source;  // the layout's object entries, 0x30 bytes each
    // The ground's camera: the quad's corners after rotation, its base and angle, two 4x4
    // 16.16 rotations about the view, the scroll origin, the texture, and the tiles shown.
    uint32_t corners_x[4];
    uint32_t corners_y[4];
    uint32_t base_x, base_y, angle;
    uint32_t matrix_a[16];
    uint32_t matrix_b[16];
    uint32_t origin_x, origin_y;
    uint32_t texture;
    uint8_t pad_900[0x38];
    uint8_t grid[6];  // +1 first column, +2 first row, +3 columns, +4 rows
    uint8_t pad_93e[0x2];
    uint32_t table;
};
static_assert(offsetof(HoleObjects, count) == 0x838);
static_assert(offsetof(HoleObjects, cursor) == 0x840);
static_assert(offsetof(HoleObjects, corners_x) == 0x848);
static_assert(offsetof(HoleObjects, base_x) == 0x868);
static_assert(offsetof(HoleObjects, matrix_a) == 0x874);
static_assert(offsetof(HoleObjects, matrix_b) == 0x8b4);
static_assert(offsetof(HoleObjects, origin_x) == 0x8f4);
static_assert(offsetof(HoleObjects, texture) == 0x8fc);
static_assert(offsetof(HoleObjects, grid) == 0x938);
static_assert(offsetof(HoleObjects, table) == 0x940);
inline HoleObjects& hole_objects_state() {
    return guest<HoleObjects>(HOLE_OBJECTS);
}

struct [[gnu::packed]] ImageRecord {
    uint32_t texture_name;   // the GL texture, once made (the texture table's records)
    uint32_t pixels;         // -> the pixels the texture was made from
    uint32_t texture_index;  // into the 60-byte texture table after TITLE_IMAGE
    uint32_t u;              // texel offset of the image inside the texture
    uint32_t v;              // texel offset of the image inside the texture
    uint32_t cell_u;         // a sprite sheet: the cell's texel offset and height
    uint32_t cell_height;
    uint32_t origin_x;  // where the image hangs from
    uint32_t origin_y;
    uint32_t word_24;
    uint8_t pad_28[0x4];
    uint32_t width;
    uint32_t height;
    uint8_t pad_34[0x4];
    uint8_t variant;  // byte 0..2: which of the image's variants to draw
};
static_assert(offsetof(ImageRecord, texture_index) == 0x8);
static_assert(offsetof(ImageRecord, u) == 0xc);
static_assert(offsetof(ImageRecord, v) == 0x10);
static_assert(offsetof(ImageRecord, cell_u) == 0x14);
static_assert(offsetof(ImageRecord, cell_height) == 0x18);
static_assert(offsetof(ImageRecord, origin_x) == 0x1c);
static_assert(offsetof(ImageRecord, origin_y) == 0x20);
static_assert(offsetof(ImageRecord, word_24) == 0x24);
static_assert(offsetof(ImageRecord, width) == 0x2c);
static_assert(offsetof(ImageRecord, height) == 0x30);
static_assert(offsetof(ImageRecord, variant) == 0x38);
static_assert(sizeof(ImageRecord) == 0x39);
inline ImageRecord& as_image(uint32_t address) {
    return guest<ImageRecord>(address);
}

// One picture that slides across the screen (a title layer, a carousel slide, the score card):
// its image, position in 16.16, and speed per frame. Seven live in the text block at +0x1cc.
struct [[gnu::packed]] Slide {
    uint32_t picture;
    uint32_t x;
    uint32_t y;
    uint32_t word_c;  // the title's bounce count
    uint32_t speed;
    uint8_t pad_14[0x1c];
};
static_assert(sizeof(Slide) == 0x30);
constexpr uint32_t SCORE_CARD_SLIDE = 5, COURSE_PICTURE_SLIDE = 6;

struct [[gnu::packed]] TextBlock {
    uint8_t pad_0[0x1cc];
    Slide slides[7];  // the pictures that slide in: title layers, carousel, score card (5), course
                      // picture (6)
    uint8_t pad_31c[0x400];
    uint8_t selection;  // byte: first active player, else 7 (inferred)
    uint8_t pad_71d[0x7];
    uint8_t carousel_count;         // byte: slides on the strip
    uint8_t score_card_rows_shown;  // byte; the card reuses the carousel's bytes
    uint8_t score_card_first;       // byte: first hole on the card
    uint8_t carousel_course;        // byte: the course in view
    uint8_t menu_return_row;        // byte: the main menu row to come back to (-1 none)
    uint8_t byte_729;
    uint8_t pad_72a[0x1];
    uint8_t byte_72b;
    uint8_t score_card_flag;  // byte: 1 = opened from the pause menu
    uint8_t byte_72d;
    uint8_t byte_72e;
    uint8_t pad_72f[0x5];
    uint32_t word_734;  // the status badge's phase
    uint32_t word_738;  // which slide the status badge is
    uint32_t word_73c;  // frames in the badge's phase
    uint8_t pad_740[0x2];
    uint8_t multiplayer;  // byte: 1 for pass 'n play
    uint8_t pad_743[0x1];
    uint8_t round_started;  // byte: the first hole of the round has begun
    uint8_t byte_745;
    uint8_t hole;  // byte: the hole to play, 0-based
    uint8_t pad_747[0x1];
    uint32_t idle_ms;  // milliseconds with no input on a screen
    uint32_t frame_count;
    uint8_t byte_750;
    uint8_t pad_751[0x3];
    uint32_t total_par;
    uint32_t total_player1;
    uint32_t total_player2;
};
static_assert(offsetof(TextBlock, slides) == 0x1cc);
static_assert(offsetof(TextBlock, selection) == 0x71c);
static_assert(offsetof(TextBlock, carousel_count) == 0x724);
static_assert(offsetof(TextBlock, score_card_rows_shown) == 0x725);
static_assert(offsetof(TextBlock, score_card_first) == 0x726);
static_assert(offsetof(TextBlock, carousel_course) == 0x727);
static_assert(offsetof(TextBlock, menu_return_row) == 0x728);
static_assert(offsetof(TextBlock, byte_729) == 0x729);
static_assert(offsetof(TextBlock, byte_72b) == 0x72b);
static_assert(offsetof(TextBlock, score_card_flag) == 0x72c);
static_assert(offsetof(TextBlock, byte_72d) == 0x72d);
static_assert(offsetof(TextBlock, byte_72e) == 0x72e);
static_assert(offsetof(TextBlock, word_734) == 0x734);
static_assert(offsetof(TextBlock, word_738) == 0x738);
static_assert(offsetof(TextBlock, word_73c) == 0x73c);
static_assert(offsetof(TextBlock, multiplayer) == 0x742);
static_assert(offsetof(TextBlock, round_started) == 0x744);
static_assert(offsetof(TextBlock, byte_745) == 0x745);
static_assert(offsetof(TextBlock, hole) == 0x746);
static_assert(offsetof(TextBlock, idle_ms) == 0x748);
static_assert(offsetof(TextBlock, frame_count) == 0x74c);
static_assert(offsetof(TextBlock, byte_750) == 0x750);
static_assert(offsetof(TextBlock, total_par) == 0x754);
static_assert(offsetof(TextBlock, total_player1) == 0x758);
static_assert(offsetof(TextBlock, total_player2) == 0x75c);
static_assert(sizeof(TextBlock) == 0x760);
inline TextBlock& as_text(uint32_t address) {
    return guest<TextBlock>(address);
}
inline TextBlock& text_block() {  // the game's own text block (GAME_STATE + game_state::TEXT)
    return guest<TextBlock>(GAME_STATE + game_state::TEXT);
}

struct [[gnu::packed]] FileRequest {
    uint8_t mode;       // byte
    uint8_t name[259];  // up to 0xff characters, NUL-terminated at +0x100
    uint32_t buffer;
    uint8_t kind;  // byte; 0 or 1 = a whole-file transfer
    uint8_t pad_109[0x3];
    uint32_t offset;  // (inferred: non-zero selects the positioned path)
    uint32_t length;
};
static_assert(offsetof(FileRequest, mode) == 0x0);      // file_request::MODE
static_assert(offsetof(FileRequest, name) == 0x1);      // file_request::NAME
static_assert(offsetof(FileRequest, buffer) == 0x104);  // file_request::BUFFER
static_assert(offsetof(FileRequest, kind) == 0x108);    // file_request::KIND
static_assert(offsetof(FileRequest, offset) == 0x10c);  // file_request::OFFSET
static_assert(offsetof(FileRequest, length) == 0x110);  // file_request::LENGTH
inline FileRequest& as_file_request(uint32_t address) {
    return guest<FileRequest>(address);
}

struct [[gnu::packed]] FileSlot {
    uint32_t id;      // the slot's index; what callers get as a handle
    uint8_t active;   // byte
    uint8_t reading;  // byte: 1 for reads, 0 for writes
    uint8_t byte_06;
    uint8_t busy;         // byte; must be 0 when closed
    FileRequest request;  // copy of the caller's request
    uint32_t result;      // the operation's result word, copied on completion
    uint32_t status;      // bytes transferred (set by the completion)
    uint8_t done;         // byte
    uint8_t pad_125[0x3f];
    uint32_t arguments[2];      // the two extra words begin_read/begin_write received
    uint32_t async_request[5];  // the Operation record (see `operation`)
    uint32_t next_free;
};
static_assert(offsetof(FileSlot, id) == 0x0);               // file_slot::ID
static_assert(offsetof(FileSlot, active) == 0x4);           // file_slot::ACTIVE
static_assert(offsetof(FileSlot, reading) == 0x5);          // file_slot::READING
static_assert(offsetof(FileSlot, byte_06) == 0x6);          // file_slot::BYTE_06
static_assert(offsetof(FileSlot, busy) == 0x7);             // file_slot::BUSY
static_assert(offsetof(FileSlot, request) == 0x8);          // file_slot::REQUEST
static_assert(offsetof(FileSlot, result) == 0x11c);         // file_slot::RESULT
static_assert(offsetof(FileSlot, status) == 0x120);         // file_slot::STATUS
static_assert(offsetof(FileSlot, done) == 0x124);           // file_slot::DONE
static_assert(offsetof(FileSlot, arguments) == 0x164);      // file_slot::ARGUMENTS
static_assert(offsetof(FileSlot, async_request) == 0x16c);  // file_slot::ASYNC_REQUEST
static_assert(offsetof(FileSlot, next_free) == 0x180);      // file_slot::NEXT_FREE
inline FileSlot& as_file_slot(uint32_t address) {
    return guest<FileSlot>(address);
}

// The WAV header read for the entry a sound bank is loading (the fields the game uses).
struct [[gnu::packed]] WavHeader {
    uint8_t pad_0[0x16];
    uint16_t channels;
    uint32_t sample_rate;
    uint8_t pad_1c[0x6];
    uint16_t bits_per_sample;
    uint8_t pad_24[0x4];
    uint32_t data_size;
};
static_assert(sizeof(WavHeader) == 0x2c);

// One entry of a sound bank's list (slot, index, file name) and its result.
struct [[gnu::packed]] SoundEntry {
    uint32_t slot, index;
    uint8_t name[0x100];
};
static_assert(sizeof(SoundEntry) == 0x108);
struct [[gnu::packed]] SoundResult {
    uint8_t status;  // 0 pending/loaded; see STATUS_* in sound_bank.cpp
    uint8_t pad_1[0x3];
    uint32_t sound;  // the sound handle, or -1
};
static_assert(sizeof(SoundResult) == 8);

struct [[gnu::packed]] SoundBank {
    uint8_t busy;       // byte: a load is in progress
    uint8_t cancelled;  // byte: stop after the entry in flight
    uint8_t pad_2[0x2];
    uint32_t entries;  // -> a copy of the caller's list
    uint32_t count;
    uint32_t cursor;  // the entry in flight
    uint32_t handle;  // the file-service handle of the read in flight
    FileRequest request;
    uint32_t callback;  // (status, results, context) when the list is done
    uint32_t context;
    uint32_t results;  // -> one result per entry
    WavHeader header;  // the WAV header read for the entry in flight
};
static_assert(offsetof(SoundBank, busy) == 0x0);        // bank::BUSY
static_assert(offsetof(SoundBank, cancelled) == 0x1);   // bank::CANCELLED
static_assert(offsetof(SoundBank, entries) == 0x4);     // bank::ENTRIES
static_assert(offsetof(SoundBank, count) == 0x8);       // bank::COUNT
static_assert(offsetof(SoundBank, cursor) == 0xc);      // bank::CURSOR
static_assert(offsetof(SoundBank, handle) == 0x10);     // bank::HANDLE
static_assert(offsetof(SoundBank, request) == 0x14);    // bank::REQUEST
static_assert(offsetof(SoundBank, callback) == 0x128);  // bank::CALLBACK
static_assert(offsetof(SoundBank, context) == 0x12c);   // bank::CONTEXT
static_assert(offsetof(SoundBank, results) == 0x130);   // bank::RESULTS
static_assert(offsetof(SoundBank, header) == 0x134);
inline SoundBank& as_bank(uint32_t address) {
    return guest<SoundBank>(address);
}

struct [[gnu::packed]] HoleObject {
    uint32_t screen_x[128];  // each frame's corners on screen (object_transform)
    uint32_t screen_y[128];
    uint32_t x;
    uint32_t y;
    uint32_t angle;
    uint32_t matrix[16];
    uint32_t quads[128];    // 32 × (a, b, c, d) words each
    uint32_t quads_b[128];  // 32 × (a, b, c, d) words each
    uint32_t kind;
    uint8_t byte_850;
    int8_t behind_ball;
    uint16_t first_frame;
    uint32_t timer;
    uint32_t elapsed;
    uint32_t wait;
    uint8_t frame;
    int8_t direction;
    uint8_t triggered;
};
static_assert(offsetof(HoleObject, screen_y) == 0x200);
static_assert(offsetof(HoleObject, x) == 0x400);            // object::X
static_assert(offsetof(HoleObject, y) == 0x404);            // object::Y
static_assert(offsetof(HoleObject, angle) == 0x408);        // object::ANGLE
static_assert(offsetof(HoleObject, matrix) == 0x40c);       // object::MATRIX
static_assert(offsetof(HoleObject, quads) == 0x44c);        // object::QUADS
static_assert(offsetof(HoleObject, quads_b) == 0x64c);      // object::QUADS_B
static_assert(offsetof(HoleObject, kind) == 0x84c);         // object::KIND
static_assert(offsetof(HoleObject, byte_850) == 0x850);     // object::BYTE_850
static_assert(offsetof(HoleObject, behind_ball) == 0x851);  // object::BEHIND_BALL
static_assert(offsetof(HoleObject, first_frame) == 0x852);  // object::FIRST_FRAME
static_assert(offsetof(HoleObject, timer) == 0x854);        // object::TIMER
static_assert(offsetof(HoleObject, elapsed) == 0x858);      // object::ELAPSED
static_assert(offsetof(HoleObject, wait) == 0x85c);         // object::WAIT
static_assert(offsetof(HoleObject, frame) == 0x860);        // object::FRAME
static_assert(offsetof(HoleObject, direction) == 0x861);    // object::DIRECTION
static_assert(offsetof(HoleObject, triggered) == 0x862);    // object::TRIGGERED
inline HoleObject& as_object(uint32_t address) {
    return guest<HoleObject>(address);
}

struct [[gnu::packed]] FileObject {
    uint32_t handle;        // the service handle, -1 when closed
    uint8_t state;          // byte: 0 idle, 1 opening, 2 closing, 3 writing, 4 reading
    uint8_t open;           // byte
    uint8_t written;        // byte: the write finished (or never started)
    uint8_t write_pending;  // byte: a write asked for while still opening
    uint8_t read_done;      // byte
    uint8_t read_pending;   // byte
    uint8_t pad_a[0x2];
    uint32_t status;  // the last completion's status
    uint32_t buffer;
    uint32_t count;
    uint32_t size;
};
static_assert(offsetof(FileObject, handle) == 0x0);         // file_object::HANDLE
static_assert(offsetof(FileObject, state) == 0x4);          // file_object::STATE
static_assert(offsetof(FileObject, open) == 0x5);           // file_object::OPEN
static_assert(offsetof(FileObject, written) == 0x6);        // file_object::WRITTEN
static_assert(offsetof(FileObject, write_pending) == 0x7);  // file_object::WRITE_PENDING
static_assert(offsetof(FileObject, read_done) == 0x8);      // file_object::READ_DONE
static_assert(offsetof(FileObject, read_pending) == 0x9);   // file_object::READ_PENDING
static_assert(offsetof(FileObject, status) == 0xc);         // file_object::STATUS
static_assert(offsetof(FileObject, buffer) == 0x10);        // file_object::BUFFER
static_assert(offsetof(FileObject, count) == 0x14);         // file_object::COUNT
static_assert(offsetof(FileObject, size) == 0x18);          // file_object::SIZE
inline FileObject& as_file_object(uint32_t address) {
    return guest<FileObject>(address);
}

// A font object is an image object — the glyph sheet — with the glyph tables after it.
struct [[gnu::packed]] FontRecord {
    ImageRecord sheet;
    uint8_t pad_39[0x3];
    uint32_t widths;
    uint32_t widths_size;
    uint32_t cell_width;
    uint32_t cell_height;
    uint32_t line_height;
    uint32_t last_code;  // above 0xff the strings are halfwords
    uint32_t advances;
    uint32_t advances_size;
};
static_assert(offsetof(FontRecord, widths) == 0x3c);         // font::WIDTHS
static_assert(offsetof(FontRecord, widths_size) == 0x40);    // font::WIDTHS_SIZE
static_assert(offsetof(FontRecord, cell_width) == 0x44);     // font::CELL_WIDTH
static_assert(offsetof(FontRecord, cell_height) == 0x48);    // font::CELL_HEIGHT
static_assert(offsetof(FontRecord, line_height) == 0x4c);    // font::LINE_HEIGHT
static_assert(offsetof(FontRecord, last_code) == 0x50);      // font::LAST_CODE
static_assert(offsetof(FontRecord, advances) == 0x54);       // font::ADVANCES
static_assert(offsetof(FontRecord, advances_size) == 0x58);  // font::ADVANCES_SIZE
inline FontRecord& as_font(uint32_t address) {
    return guest<FontRecord>(address);
}

struct [[gnu::packed]] PackRecord {
    uint32_t largest;  // the largest resource the scratch buffer can hold
    uint32_t scratch;  // -> that buffer
    uint32_t file_a;   // entries below COUNT_A
    uint32_t table_a;  // entries below COUNT_A
    uint32_t table_a_bytes;
    uint32_t file_b;   // the rest
    uint32_t table_b;  // the rest
    uint32_t count_b_bytes;
    uint32_t count_a;
    uint8_t name[0x34];  // the pack's name, which its files' names are built from
};
static_assert(offsetof(PackRecord, largest) == 0x0);         // pack::LARGEST
static_assert(offsetof(PackRecord, scratch) == 0x4);         // pack::SCRATCH
static_assert(offsetof(PackRecord, file_a) == 0x8);          // pack::FILE_A
static_assert(offsetof(PackRecord, table_a) == 0xc);         // pack::TABLE_A
static_assert(offsetof(PackRecord, table_a_bytes) == 0x10);  // pack::TABLE_A_BYTES
static_assert(offsetof(PackRecord, name) == 0x24);           // pack::NAME
static_assert(sizeof(PackRecord) == 0x58);                   // pack::SIZE
static_assert(offsetof(PackRecord, file_b) == 0x14);         // pack::FILE_B
static_assert(offsetof(PackRecord, table_b) == 0x18);        // pack::TABLE_B
static_assert(offsetof(PackRecord, count_b_bytes) == 0x1c);  // pack::COUNT_B_BYTES
static_assert(offsetof(PackRecord, count_a) == 0x20);        // pack::COUNT_A
inline PackRecord& as_pack(uint32_t address) {
    return guest<PackRecord>(address);
}

struct [[gnu::packed]] AsyncRequest {
    uint8_t pad_0[0x4];
    uint8_t state;  // byte: the operation code
    uint8_t pad_5[0x3];
    uint32_t file_object;  // -> the Operation record
    uint32_t seek_offset;
    uint8_t byte_10;
    uint8_t pad_11[0x3];
    uint32_t buffer;
    uint32_t length;
    uint8_t stage;  // byte: 0 new, 1 armed, 2 attached
    uint8_t pad_1d[0x3];
    uint32_t status;
    uint32_t size_result;
    uint32_t result;
    uint32_t file_handle;
    uint32_t word_30;
    uint32_t callback;
    uint32_t context;
};
static_assert(offsetof(AsyncRequest, word_30) == 0x30);      // async_request::WORD_30
static_assert(offsetof(AsyncRequest, state) == 0x4);         // async_request::STATE
static_assert(offsetof(AsyncRequest, file_object) == 0x8);   // async_request::FILE_OBJECT
static_assert(offsetof(AsyncRequest, seek_offset) == 0xc);   // async_request::SEEK_OFFSET
static_assert(offsetof(AsyncRequest, byte_10) == 0x10);      // async_request::BYTE_10
static_assert(offsetof(AsyncRequest, buffer) == 0x14);       // async_request::BUFFER
static_assert(offsetof(AsyncRequest, length) == 0x18);       // async_request::LENGTH
static_assert(offsetof(AsyncRequest, stage) == 0x1c);        // async_request::STAGE
static_assert(offsetof(AsyncRequest, status) == 0x20);       // async_request::STATUS
static_assert(offsetof(AsyncRequest, size_result) == 0x24);  // async_request::SIZE_RESULT
static_assert(offsetof(AsyncRequest, result) == 0x28);       // async_request::RESULT
static_assert(offsetof(AsyncRequest, file_handle) == 0x2c);  // async_request::FILE_HANDLE
static_assert(offsetof(AsyncRequest, callback) == 0x34);     // async_request::CALLBACK
static_assert(offsetof(AsyncRequest, context) == 0x38);      // async_request::CONTEXT
inline AsyncRequest& as_request(uint32_t address) {
    return guest<AsyncRequest>(address);
}

struct [[gnu::packed]] Operation {
    uint32_t file_handle;  // from the request once an open completes
    uint8_t state;         // byte: 0 idle, 1 opening, 2 open, 3 closing, 4 transferring
    uint8_t pad_5[0x3];
    uint32_t result;
    uint32_t callback;  // slot-level completion and its context
    uint32_t context;
};
static_assert(offsetof(Operation, file_handle) == 0x0);  // operation::FILE_HANDLE
static_assert(offsetof(Operation, state) == 0x4);        // operation::STATE
static_assert(offsetof(Operation, result) == 0x8);       // operation::RESULT
static_assert(offsetof(Operation, callback) == 0xc);     // operation::CALLBACK
static_assert(offsetof(Operation, context) == 0x10);     // operation::CONTEXT
inline Operation& as_operation(uint32_t address) {
    return guest<Operation>(address);
}

struct [[gnu::packed]] ObjectKind {
    uint32_t animation;  // 0 none, 1 once, 2 loop, 3 back and forth
    uint32_t rate;
    uint32_t trigger;
    uint32_t wait;
    uint32_t frame_count;
    uint32_t meshes[32];
};
static_assert(offsetof(ObjectKind, animation) == 0x0);     // kind::ANIMATION
static_assert(offsetof(ObjectKind, rate) == 0x4);          // kind::RATE
static_assert(offsetof(ObjectKind, trigger) == 0x8);       // kind::TRIGGER
static_assert(offsetof(ObjectKind, wait) == 0xc);          // kind::WAIT
static_assert(offsetof(ObjectKind, frame_count) == 0x10);  // kind::FRAME_COUNT
static_assert(offsetof(ObjectKind, meshes) == 0x14);       // kind::MESHES
inline ObjectKind& as_kind(uint32_t address) {
    return guest<ObjectKind>(address);
}

struct [[gnu::packed]] RandomState {
    uint32_t seed;
    uint32_t last;
    uint32_t state[625];  // 624 words; the first is the seed made odd
    uint32_t next;        // -> the next word to draw
    uint32_t left;        // words left before the state is regenerated
    uint8_t seeded;       // byte
};
static_assert(offsetof(RandomState, seed) == 0x0);      // random::SEED
static_assert(offsetof(RandomState, last) == 0x4);      // random::LAST
static_assert(offsetof(RandomState, state) == 0x8);     // random::STATE
static_assert(offsetof(RandomState, next) == 0x9cc);    // random::NEXT
static_assert(offsetof(RandomState, left) == 0x9d0);    // random::LEFT
static_assert(offsetof(RandomState, seeded) == 0x9d4);  // random::SEEDED
inline RandomState& as_random(uint32_t address) {
    return guest<RandomState>(address);
}

struct [[gnu::packed]] BitStream {
    uint32_t out;
    uint32_t in;
    uint32_t bits;
    uint32_t current;
    uint32_t written;
    uint32_t run;
    uint32_t escape;
    uint32_t code_bits;
    uint32_t prefix_limit;
    uint32_t long_run;
    uint32_t offset_bits;
    uint8_t table[256];
};
static_assert(offsetof(BitStream, out) == 0x0);            // stream::OUT
static_assert(offsetof(BitStream, in) == 0x4);             // stream::IN
static_assert(offsetof(BitStream, bits) == 0x8);           // stream::BITS
static_assert(offsetof(BitStream, current) == 0xc);        // stream::CURRENT
static_assert(offsetof(BitStream, written) == 0x10);       // stream::WRITTEN
static_assert(offsetof(BitStream, run) == 0x14);           // stream::RUN
static_assert(offsetof(BitStream, escape) == 0x18);        // stream::ESCAPE
static_assert(offsetof(BitStream, code_bits) == 0x1c);     // stream::CODE_BITS
static_assert(offsetof(BitStream, prefix_limit) == 0x20);  // stream::PREFIX_LIMIT
static_assert(offsetof(BitStream, long_run) == 0x24);      // stream::LONG_RUN
static_assert(offsetof(BitStream, offset_bits) == 0x28);   // stream::OFFSET_BITS
static_assert(offsetof(BitStream, table) == 0x2c);         // stream::TABLE
inline BitStream& as_stream(uint32_t address) {
    return guest<BitStream>(address);
}

struct [[gnu::packed]] MenuItem {
    uint32_t text_id;
    uint32_t kind;   // 0..6: what selecting it does (menu.cpp)
    uint32_t x;      // 16.16 screen position during the slide
    uint32_t y;      // 16.16
    uint32_t delay;  // frames before this item starts sliding; -1 = done
    uint32_t style;  // 1 = a heading row, 2 = the course name row
};
static_assert(offsetof(MenuItem, text_id) == 0x0);  // menu_item::TEXT_ID
static_assert(offsetof(MenuItem, kind) == 0x4);     // menu_item::KIND
static_assert(offsetof(MenuItem, x) == 0x8);        // menu_item::X
static_assert(offsetof(MenuItem, y) == 0xc);        // menu_item::Y
static_assert(offsetof(MenuItem, delay) == 0x10);   // menu_item::DELAY
static_assert(offsetof(MenuItem, style) == 0x14);   // menu_item::STYLE
// The live menu item tables. The original built them in the game state block at fixed
// addresses (MENU_TABLE, MENU_TABLE_ALT, and the score card's two rows in the screen object's
// padding); nothing outside the game ever read them, so they are host state (menu.cpp).
constexpr uint32_t MENU_TABLE_ROWS = 18;  // the hole-select table, the longest one
MenuItem* menu_table();                   // the table a screen builds
MenuItem* menu_table_alt();    // the main menu without its "resume" row: menu_table() + 1
MenuItem* card_items_table();  // the "save game?" card's two rows
MenuItem*& menu_items();       // the table the current screen shows; null when there is none
void menu_items_load(MenuItem* into, uint32_t image_items, uint32_t bytes);

struct [[gnu::packed]] FileEntry {
    uint32_t text;  // which language text and entry of the file-kind table this is
    uint32_t entry;
    uint32_t name;        // -> record: +0 name string, +4 total size
    uint32_t chunks[10];  // ten pointers
    uint32_t sizes[10];   // ten sizes
    uint32_t cursor;      // -> the next byte to read
    uint32_t offset;      // into the current chunk
    uint32_t chunk;       // the current chunk's index
};
static_assert(offsetof(FileEntry, entry) == 0x4);
static_assert(offsetof(FileEntry, name) == 0x8);     // file::NAME
static_assert(offsetof(FileEntry, chunks) == 0xc);   // file::CHUNKS
static_assert(offsetof(FileEntry, sizes) == 0x34);   // file::SIZES
static_assert(offsetof(FileEntry, cursor) == 0x5c);  // file::CURSOR
static_assert(offsetof(FileEntry, offset) == 0x60);  // file::OFFSET
static_assert(offsetof(FileEntry, chunk) == 0x64);   // file::CHUNK
inline FileEntry& as_file(uint32_t address) {
    return guest<FileEntry>(address);
}

struct [[gnu::packed]] CoursePicture {
    uint8_t pad_0[0x8];
    uint32_t use_count;  // the pack keeps a reference count; the placeholder has none
    uint8_t pad_c[0x20];
    uint32_t width;
    uint32_t height;
    uint8_t pad_34[0x4];
    uint8_t locked;  // byte: drawn as the placeholder
};
static_assert(offsetof(CoursePicture, use_count) == 0x8);  // course_picture::USE_COUNT
static_assert(offsetof(CoursePicture, width) == 0x2c);     // course_picture::WIDTH
static_assert(offsetof(CoursePicture, height) == 0x30);    // course_picture::HEIGHT
static_assert(offsetof(CoursePicture, locked) == 0x38);    // course_picture::LOCKED
inline CoursePicture& as_course_picture(uint32_t address) {
    return guest<CoursePicture>(address);
}

struct [[gnu::packed]] GameState {
    uint32_t handle;          // result of 0x18008c78(1, 1) at start-up; asserted non-zero
    uint32_t pack_handle;     // the "jdmg" resource pack
    uint32_t pack_title;      // the "jdmgsheets" pack: the title images
    uint32_t pack_course[3];  // the "c00".."c02" packs, by course
    uint32_t pack_sheets[3];
    uint32_t object_24;  // from 0x18008408(RESOURCE_ID_A); +4 from 0x18008454
    uint32_t word_28;    // set to 0x82b80 at start-up (inferred)
    uint8_t pad_2c[0x82b80];
    uint32_t object_82bac;  // object from 0x18007744 during resource loading
    uint8_t pad_82bb0[0x150];
    uint8_t screen[128];   // the running screen's block
    uint8_t save_data[5];  // 0x144 bytes, the live save game
    uint8_t save_data_byte_5;
    uint8_t pad_82d86[0x14];
    uint8_t byte_82d9a;
    uint8_t pad_82d9b[0x34];
    uint8_t save_data_byte_4f;  // cleared before a screen is entered
    uint8_t pad_82dd0[0xf0];
    uint32_t save_magic;   // SAVE_MAGIC, written just before the buffer
    uint32_t save_buffer;  // 0x148 bytes, the file image being read or written
    uint8_t pad_82ec8[0x141];
    uint8_t byte_83009;
    uint8_t pad_8300a[0x2];
    uint8_t save_write_started;
    uint8_t save_write_second;
    uint8_t pad_8300e[0xff2];
    uint32_t course_table[1024];  // 11 records of 60 bytes; +0xa10 in each set to -1 at start-up
    uint32_t text[10240];         // text / font block
    uint32_t settings[344];       // options and audio levels
    uint32_t carousel_scroll;     // 16.16: the filmstrip's scroll position
    uint8_t pad_8f564[0xc4];
    uint32_t word_8f628;
    uint8_t pad_8f62c[0xd4];
    int8_t loaded[256];  // +0xbe must be 1 before phase 6; +0xc8/+0xc9 entry counters
};
static_assert(offsetof(GameState, handle) == 0x0);
static_assert(offsetof(GameState, pack_handle) == 0x4);
static_assert(offsetof(GameState, pack_title) == 0x8);
static_assert(offsetof(GameState, pack_course) == 0xc);
static_assert(offsetof(GameState, pack_sheets) == 0x18);
static_assert(offsetof(GameState, object_24) == 0x24);
static_assert(offsetof(GameState, word_28) == 0x28);
static_assert(offsetof(GameState, object_82bac) == 0x82bac);
static_assert(offsetof(GameState, screen) == 0x82d00);
static_assert(offsetof(GameState, save_data) == 0x82d80);
static_assert(offsetof(GameState, save_data_byte_5) == 0x82d85);
static_assert(offsetof(GameState, byte_82d9a) == 0x82d9a);
static_assert(offsetof(GameState, save_data_byte_4f) == 0x82dcf);
static_assert(offsetof(GameState, save_magic) == 0x82ec0);
static_assert(offsetof(GameState, save_buffer) == 0x82ec4);
static_assert(offsetof(GameState, byte_83009) == 0x83009);
static_assert(offsetof(GameState, save_write_started) == 0x8300c);
static_assert(offsetof(GameState, save_write_second) == 0x8300d);
static_assert(offsetof(GameState, course_table) == 0x84000);
static_assert(offsetof(GameState, text) == 0x85000);
static_assert(offsetof(GameState, settings) == 0x8f000);
static_assert(offsetof(GameState, carousel_scroll) == 0x8f560);
static_assert(offsetof(GameState, word_8f628) == 0x8f628);
static_assert(offsetof(GameState, loaded) == 0x8f700);
static_assert(sizeof(GameState) == 0x8f800);
inline GameState& game_state_block() {
    return guest<GameState>(GAME_STATE);
}

// A byte of the live save record, by its offset from the record's base. The record is 0x144
// bytes and the structure names only the few fields reached through it, so `save_data[]` itself
// is short and indexing it past its end is out of that array's bounds.
inline uint8_t& save_data_byte(uint32_t offset) {
    return guest<uint8_t>(GAME_STATE + offsetof(GameState, save_data) + offset);
}

// A byte of the running screen's block, by its offset from the block's own base. Two of the
// offsets the game uses (game_state::SCREEN_BYTE_9A and SCREEN_ID) are past the 128 bytes the
// block was first given, so reaching them through `screen[]` is out of that array's bounds —
// true of the original's own addressing too, since it works from the base with a constant.
inline uint8_t& screen_block_byte(uint32_t offset) {
    return guest<uint8_t>(GAME_STATE + offsetof(GameState, screen) + offset);
}

struct [[gnu::packed]] AnswerBlock {
    uint8_t state;  // byte; 5 = asks to suspend, 6 = suspended (inferred)
    uint8_t pad_1[0x1f];
    uint32_t button_event;  // word handed to the button dispatcher
    uint32_t idle_answer;   // the tick's answer when the app went idle
    uint8_t idle;           // the app is idle
    uint8_t resume_flag_a;  // set when a long Next hold ends a suspend (as_answer(inferred))
    uint8_t resume_flag_b;  // set when a long Menu hold ends a suspend (as_answer(inferred))
};
static_assert(offsetof(AnswerBlock, state) == 0x0);  // answer::STATE
static_assert(offsetof(AnswerBlock, button_event) == 0x20);
static_assert(offsetof(AnswerBlock, idle_answer) == 0x24);
static_assert(offsetof(AnswerBlock, idle) == 0x28);           // answer::BUTTON_EVENT
static_assert(offsetof(AnswerBlock, resume_flag_a) == 0x29);  // answer::RESUME_FLAG_A
static_assert(offsetof(AnswerBlock, resume_flag_b) == 0x2a);  // answer::RESUME_FLAG_B
inline AnswerBlock& as_answer(uint32_t address) {
    return guest<AnswerBlock>(address);
}

struct [[gnu::packed]] ContextBlock {
    uint8_t state;  // byte; the game copies the answer's state here each frame
    uint8_t pad_1[0x3];
    uint32_t clock;           // microseconds at frame start (miscTBD #9 writes it)
    uint32_t frame_duration;  // microseconds the frame's update took
    uint8_t pad_c[0x8];
    uint32_t firmware_word_14;  // copied into the step input
    uint32_t firmware_word_18;
    uint8_t pad_1c[0x10];
    uint32_t completed_requests;  // list of file requests completed this frame
    uint32_t event_list_head;     // head of the firmware's event list for this frame
    uint32_t word_34;             // cleared on the first frame
};
static_assert(offsetof(ContextBlock, state) == 0x0);  // context::STATE
static_assert(offsetof(ContextBlock, clock) == 0x4);  // context::CLOCK
static_assert(offsetof(ContextBlock, frame_duration) == 0x8);
static_assert(offsetof(ContextBlock, firmware_word_14) == 0x14);
static_assert(offsetof(ContextBlock, word_34) == 0x34);             // context::FRAME_DURATION
static_assert(offsetof(ContextBlock, completed_requests) == 0x2c);  // context::COMPLETED_REQUESTS
static_assert(offsetof(ContextBlock, event_list_head) == 0x30);     // context::EVENT_LIST_HEAD
inline ContextBlock& as_context(uint32_t address) {
    return guest<ContextBlock>(address);
}

struct [[gnu::packed]] FileService {
    uint8_t pad_0[0xf24];
    uint32_t word_f24;
    uint32_t free_list;  // first free slot, linked through slot::NEXT_FREE
};
static_assert(offsetof(FileService, word_f24) == 0xf24);   // file_service::WORD_F24
static_assert(offsetof(FileService, free_list) == 0xf28);  // file_service::FREE_LIST
inline FileService& as_file_service(uint32_t address) {
    return guest<FileService>(address);
}

// The seven wheel/button slots: the two wheel directions then the five buttons. Host state
// (the original kept them at PLAYER_TABLE; nothing outside the game ever read them).
// flags bit 0 = held last frame, bit 1 = held now, bit 2 = just pressed, bit 3 = just released;
// the count grows while held and restarts on a press; the step is how far the wheel turned.
struct [[gnu::packed]] WheelSlot {
    uint16_t flags;
    uint16_t count;
    uint32_t step;
};
constexpr uint32_t WHEEL_SLOT_COUNT = 7;
// Forget every slot: a screen opens with no input pending.
void wheel_slots_clear();

inline WheelSlot& wheel_slot_at(uint32_t index) {
    static WheelSlot slots[WHEEL_SLOT_COUNT];
    return slots[index];
}

// This frame's input (INPUT_SNAPSHOT), and the copy game_step works from (STEP_INPUT).
struct [[gnu::packed]] InputSnapshot {
    int32_t wheel_movement;  // signed detents this step
    uint32_t flags;
    uint32_t wheel_position;  // 16.16
    uint8_t pad_c[0x4];
    uint32_t firmware_words[2];  // context +0x14 and +0x18
    uint32_t event_list;
};
static_assert(sizeof(InputSnapshot) == snapshot::COPY_SIZE);
inline InputSnapshot& input_snapshot() {
    return guest<InputSnapshot>(INPUT_SNAPSHOT);
}
inline InputSnapshot& step_input() {
    return guest<InputSnapshot>(STEP_INPUT);
}
inline InputSnapshot& as_snapshot(uint32_t address) {
    return guest<InputSnapshot>(address);
}

// A node of the firmware's event / completed-request lists (see game_state.h `request`).
struct [[gnu::packed]] EventNode {
    uint8_t type;   // for button events 1..5 = Menu, Select, Previous, Play, Next
    uint8_t state;  // 1 = released, 2 = pressed
    uint8_t pad_2[0x6];
    uint32_t next;
    uint8_t pad_c[0x10];
    uint8_t in_use;
    uint8_t pad_1d[0x3];
    uint32_t owner;  // non-zero: the owner keeps it after completion (inferred)
    uint8_t pad_24[0x4];
    uint32_t result;  // -1 when none; otherwise handed to the next node (inferred)
    uint8_t pad_2c[0x8];
    uint32_t callback;
    uint32_t context;
};
static_assert(offsetof(EventNode, next) == request::NEXT);
static_assert(offsetof(EventNode, in_use) == request::IN_USE);
static_assert(offsetof(EventNode, result) == request::RESULT);
static_assert(offsetof(EventNode, context) == request::CONTEXT);
// The option page's scratch (0x180379ec): whether the options changed, and the last wheel
// steps the volume and brightness sliders followed.
struct [[gnu::packed]] OptionsScratch {
    uint8_t changed;
    uint8_t pad_1[0x3];
    uint32_t wheel_step_last;
    uint32_t brightness_step_last;
};
inline OptionsScratch& options_scratch() {
    return guest<OptionsScratch>(OPTIONS_CHANGED);
}

// The hole badge's flags (0x18034084): whether the score card covers it, and whether to show it.
struct [[gnu::packed]] BadgeFlags {
    uint8_t covered;
    uint8_t shown;
};
inline BadgeFlags& badge_flags() {
    return guest<BadgeFlags>(0x1803'4084);
}

inline EventNode& as_event(uint32_t address) {
    return guest<EventNode>(address);
}

struct [[gnu::packed]] MeshRecord {
    uint8_t pad_0[0x8];
    uint32_t texture_index;
    uint8_t pad_c[0x8];
    uint32_t u;
    uint32_t v;
    uint32_t width;
    uint32_t left;
    uint32_t top;
    uint32_t height;
};
static_assert(offsetof(MeshRecord, texture_index) == 0x8);  // mesh::TEXTURE_INDEX
static_assert(offsetof(MeshRecord, u) == 0x14);             // mesh::U
static_assert(offsetof(MeshRecord, v) == 0x18);             // mesh::V
static_assert(offsetof(MeshRecord, width) == 0x1c);         // mesh::WIDTH
static_assert(offsetof(MeshRecord, left) == 0x20);          // mesh::LEFT
static_assert(offsetof(MeshRecord, top) == 0x24);           // mesh::TOP
static_assert(offsetof(MeshRecord, height) == 0x28);        // mesh::HEIGHT
inline MeshRecord& as_mesh(uint32_t address) {
    return guest<MeshRecord>(address);
}

// A player's round (MENU + 0x60, one per player 0x28 bytes apart): strokes by hole and on this
// hole, how the hole ended, and where the ball came to rest between turns.
struct [[gnu::packed]] PlayerRecord {
    int8_t strokes_by_hole[18];  // from MENU + 0x60; a signed byte per hole
    uint16_t strokes;            // halfword: on this hole
    int16_t gender;              // halfword: 0 female, 1 male, for this round
    uint16_t result;             // halfword: 1 holed in one, 2 holed, -1 limit (inferred)
    uint32_t ball_rest_x, ball_rest_y, ball_rest_angle;  // between turns
    uint8_t placed;  // byte: the first putt's ball has been placed
    uint8_t pad_25[0x3];
};
static_assert(sizeof(PlayerRecord) == PLAYER_STRIDE);
static_assert(offsetof(PlayerRecord, strokes) == player::STROKES - player::STROKES_BY_HOLE);
static_assert(offsetof(PlayerRecord, gender) == 0x14);
static_assert(offsetof(PlayerRecord, result) == player::RESULT - player::STROKES_BY_HOLE);
static_assert(offsetof(PlayerRecord, ball_rest_x) == player::BALL_REST_X - player::STROKES_BY_HOLE);
static_assert(offsetof(PlayerRecord, placed) == player::PLACED - player::STROKES_BY_HOLE);
inline PlayerRecord& player_record(uint32_t index) {
    return guest<PlayerRecord>(MENU + player::STROKES_BY_HOLE + index * PLAYER_STRIDE);
}

}  // namespace minigolf::game
