// Playing a course: entering a hole, starting a course, and the score card. The hole itself
// runs as screen 11, whose tick (0x18002c28) is a state machine over SETTINGS_WORD_628 and
// whose render (0x1800a080) draws the course — both still recompiled.
#include "course.h"

#include "calling.h"
#include "cheats.h"
#include "draw.h"
#include "fixed.h"
#include "game_state.h"
#include "hole_load.h"
#include "hole_render.h"
#include "hole_tick.h"
#include "init.h"
#include "libc.h"
#include "pause_menu.h"
#include "records.h"
#include "resources.h"
#include "round_history.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "state.h"

namespace minigolf::game {

uint32_t save_record_snapshot();
void hole_setup();
void ball_rest_record();
void files_release(uint32_t all);
void sound_slot_stop(uint32_t slot, uint32_t index);
void sound_slot_release(uint32_t slot, uint32_t index);
uint32_t random_next(uint32_t object, uint32_t modulus);
void pause_menu_enter();
void screen_set_tail(uint32_t id);

void music_start(uint32_t in_game);

namespace {

// Still recompiled, named by their use here (inferred).
constexpr uint32_t MESHES = GAME_STATE + 0x8b944, MESH_SIZE = 60, MESH_LIMIT = 0x100;
constexpr uint32_t SHEET_SIZE = 0x48, SAVE_RECORD_SIZE = 0x144;

constexpr uint32_t SCREEN_HOLE = 11, SCREEN_COURSE_LOADING = 13;
constexpr uint32_t SCORE_CARD_ROWS = 9;
constexpr uint32_t PLAY_STATE_SCORE_CARD = 0x15;  // SETTINGS_WORD_628: what the hole tick is doing
constexpr uint32_t DROP_FRAMES_FIXED = to_fixed(30);
constexpr uint32_t NO_VALUE = 0xffff'ffffu;

}  // namespace

// 0x1800291c — into the hole: its tick and render, no button handler (the tick reads the
// wheel and buttons itself), and no input carried over.
void hole_enter() {
    screen_install(nullptr, hole_screen_tick, hole_render, nullptr);
    screen_set(SCREEN_HOLE);
}

// 0x180026ac — start the chosen course at the chosen hole: load the hole, then the loading
// screen (13) takes over until its objects are built. `resuming` is remembered for the flow.
void course_start(uint32_t resuming) {
    const int32_t course = static_cast<int32_t>(menu_state().course);
    if (game_state_block().pack_course[static_cast<uint32_t>(course)] == 0) {
        assert_trap(0x180026ccu);
    }
    if (course < 0 || course >= 3) {
        assert_trap(course < 0 ? 0x180026d8u : 0x180026e4u);
    }
    const int32_t hole = static_cast<int32_t>(menu_state().hole);
    if (hole < 0 || hole >= static_cast<int32_t>(HOLES_PER_COURSE)) {
        assert_trap(hole < 0 ? 0x180026f4u : 0x18002700u);
    }
    play_state().resuming = static_cast<uint8_t>(resuming);
    screen_state().course_loading = static_cast<uint8_t>(static_cast<uint32_t>(course));
    play_state().word_638 = NO_VALUE;
    menu_state().word_b0 = NO_VALUE;
    music_start(1);
    hole_layout_load();
    course_data_load();
    screen_set(SCREEN_COURSE_LOADING);
    play_state().ticked = static_cast<uint8_t>(0);
    play_state().wheel_repeats = 0;
}

// 0x18002968 — open the score card (a state of the hole's tick, not a screen of its own):
// the course picture drops in from above, and the totals — par, each player's strokes — are
// summed for the card. From the pause menu it first re-enters the hole.
void score_card_open() {
    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    if (static_cast<uint32_t>(text.score_card_flag) == 1) {
        hole_enter();
        wheel_slots_clear();  // hole_enter
    }
    game_state_block().word_8f628 = PLAY_STATE_SCORE_CARD;

    // The picture's drop, the same way the title's layers arrive.
    const uint32_t picture = COURSE_PICTURES;
    text_block().slides[SCORE_CARD_SLIDE].picture = picture;
    text_block().frame_count = 0;
    text_block().slides[SCORE_CARD_SLIDE].x = 0;
    const int32_t height = static_cast<int32_t>(as_course_picture(picture).height);
    text_block().slides[SCORE_CARD_SLIDE].y = to_fixed(-height);
    const int64_t numerator = static_cast<int64_t>(height) << 32;
    text_block().slides[SCORE_CARD_SLIDE].speed =
        libc::divide64(static_cast<uint32_t>(numerator), static_cast<uint32_t>(numerator >> 32),
                       DROP_FRAMES_FIXED, 0);

    // Which holes the card shows: nine at a time, starting near the current hole.
    const int32_t hole = static_cast<int32_t>(menu_state().hole);
    const int32_t first = static_cast<int8_t>(hole - 8);
    text.carousel_count = static_cast<int8_t>(HOLES_PER_COURSE);
    text.score_card_rows_shown = static_cast<int8_t>(SCORE_CARD_ROWS);
    text.score_card_first = static_cast<uint8_t>(first < 0 ? 0u : static_cast<uint32_t>(first));
    text.carousel_course =
        static_cast<uint8_t>(static_cast<int8_t>(first < 0 ? 0u : static_cast<uint32_t>(first)));
    if (static_cast<int32_t>(static_cast<uint32_t>(menu_state().cursor)) + 9 >
        static_cast<int32_t>(HOLES_PER_COURSE)) {
        text.carousel_course = static_cast<int8_t>(SCORE_CARD_ROWS);
    }
    if (static_cast<uint32_t>(menu_state().game_mode) ==
        MODE_PRACTICE_HOLE) {  // one hole: show just it
        text.carousel_count = static_cast<int8_t>(1);
        text.score_card_rows_shown = static_cast<int8_t>(1);
        text.score_card_first = static_cast<uint8_t>(static_cast<uint32_t>(hole));
        text.carousel_course =
            static_cast<uint8_t>(static_cast<int8_t>(static_cast<uint32_t>(hole)));
    }

    // The totals.
    const int32_t course = static_cast<int32_t>(menu_state().course);
    const int8_t* pars = course_info_at(course).pars;
    int32_t total_par = 0, total_player1 = 0, total_player2 = 0;
    text.total_par = 0;
    text.total_player1 = 0;
    text.total_player2 = 0;
    for (uint32_t i = 0; i < HOLES_PER_COURSE; ++i) {
        total_par += pars[i];
        text.total_par = static_cast<uint32_t>(total_par);
        total_player1 +=
            static_cast<int32_t>(static_cast<uint32_t>(menu_state().strokes_player1[i]));
        text.total_player1 = static_cast<uint32_t>(total_player1);
        total_player2 +=
            static_cast<int32_t>(static_cast<uint32_t>(menu_state().strokes_player2[i]));
        text.total_player2 = static_cast<uint32_t>(total_player2);
    }
}

// 0x18002ac4 — let a hole's course data go: the mesh textures, the objects and sprite sheets
// (their images released, their tables freed), the frame tables, pegs, walls and animated
// objects, and the course data itself.
void course_unload() {
    for (uint32_t t = 0; t < MESH_LIMIT; ++t) {
        texture_release(as_image(MESHES + t * MESH_SIZE));
    }
    libc::memory_clear(MESHES, MESH_LIMIT * MESH_SIZE);
    if (hole_objects_state().table != 0) {
        tracked_free((hole_objects_state().table));
        hole_objects_state().table = 0;
    }
    const auto sheets_release = [&](uint32_t table, uint16_t count) {
        if (table == 0) {
            return;
        }
        for (int32_t i = 0; i < static_cast<int16_t>(count); ++i) {
            texture_release(as_image(table + static_cast<uint32_t>(i) * SHEET_SIZE +
                                     static_cast<uint32_t>(offsetof(SpriteSheet, image))));
        }
    };
    sheets_release(play_state().sprite_sheets_b, players_state().sprite_sheet_b_count);
    sheets_release(play_state().sprite_sheets, players_state().sprite_sheet_count);
    // Each table is freed and its pointer cleared. The pointers are taken by address rather than
    // by reference: they are fields of a packed overlay, and a reference to one of those is not
    // something every compiler will give (aarch64-none-elf-g++ refuses).
    const auto release = [](uint32_t at) {
        uint32_t& table = guest<uint32_t>(at);
        if (table != 0) {
            tracked_free(table);
            table = 0;
        }
    };
    const auto play_field = [](size_t field) { return PLAY + static_cast<uint32_t>(field); };
    release(play_field(offsetof(PlayState, frame_counts_a)));
    release(play_field(offsetof(PlayState, sprite_sheets)));
    release(play_field(offsetof(PlayState, peg_table)));
    release(play_field(offsetof(PlayState, wall_table)));
    release(play_field(offsetof(PlayState, objects)));
    release(COURSE_DATA);
}

// 0x18004a3c — keep a copy of the save record beside it, for the save file to be written from
// while play goes on. Always answers 1.
uint32_t save_record_snapshot() {
    libc::memory_copy(GAME_STATE + game_state::SAVE_DATA + SAVE_RECORD_SIZE,
                      GAME_STATE + game_state::SAVE_DATA, SAVE_RECORD_SIZE);
    return 1;
}

// 0x18004144 — a saved round taken up again: the strokes by hole, the current strokes,
// result and hole, the ball's resting place and the hole's state, from the save record (and
// the player's gender from the option, or from the record when the option says random).
void course_resume() {
    if (static_cast<uint32_t>(screen_state().byte_d85) != 1) {
        assert_trap(0x18004154u);
    }
    const SaveRecord& record = save_record();
    const uint32_t text = GAME_STATE + game_state::TEXT;
    for (uint32_t hole = 0; hole < HOLES_PER_COURSE; ++hole) {
        player_record(0).strokes_by_hole[hole] = record.strokes_by_hole[hole];
    }
    player_record(0).strokes =
        static_cast<uint16_t>(static_cast<uint32_t>(static_cast<int8_t>(record.strokes)));
    player_record(0).ball_rest_x = screen_state().saved_ball_x;
    player_record(0).ball_rest_y = screen_state().saved_ball_y;
    const int32_t gender_option =
        static_cast<int32_t>(static_cast<uint32_t>(options_state().player_gender));
    uint32_t gender = gender_option == 2 ? 1 : 0;
    if (gender_option == 0) {
        gender = record.gender;
    }
    menu_state().player_gender = static_cast<uint16_t>(gender);
    player_record(0).result = record.result;
    as_text(text).hole = record.resume_hole;
    play_state().state = screen_state().saved_state;
}

// 0x18004998 — the hole's state into the save record, for the round to be taken up again:
// the strokes by hole, the course, hole, strokes, result and gender, the ball's rest and the
// hole's state; `resumable` marks it, and a finished round is no longer one to resume.
void course_state_save(uint32_t resumable) {
    SaveRecord& record = save_record();
    for (uint32_t hole = 0; hole < HOLES_PER_COURSE; ++hole) {
        record.strokes_by_hole[hole] = player_record(0).strokes_by_hole[hole];
    }
    if (static_cast<int32_t>(menu_state().hole) == static_cast<int32_t>(HOLES_PER_COURSE)) {
        screen_state().byte_d85 = static_cast<uint8_t>(0);
    }
    record.strokes = static_cast<uint8_t>(player_record(0).strokes);
    record.resume_course = static_cast<uint8_t>(menu_state().course);
    record.resume_hole = static_cast<uint8_t>(menu_state().hole);
    screen_state().saved_state = play_state().state;
    record.result = player_record(0).result;
    screen_state().byte_dd0 = static_cast<uint8_t>(resumable);
    screen_state().saved_ball_x = player_record(0).ball_rest_x;
    screen_state().saved_ball_y = player_record(0).ball_rest_y;
    record.gender = menu_state().player_gender;
    save_record_snapshot();  // a tail call in the original
}

// 0x18009e90 — a new round: pass 'n play noted, player 0 up, the single-player course record
// cleared, the player table cleared, and the player's gender from the option (random when it
// says so; pass 'n play gives the two players one each).
void game_new() {
    // A fresh round starts honest, unless a rule-changing cheat is already on (cheats.h), and
    // with nothing yet recorded of the ball's path (round_history.h).
    round_records_reset();
    round_begin();
    const uint32_t text = GAME_STATE + game_state::TEXT;
    as_text(text).multiplayer = static_cast<uint8_t>(1);
    players_state().current = static_cast<int16_t>(0);
    if (static_cast<uint32_t>(menu_state().game_mode) == 0) {
        screen_state().word_d9c = 0;
    }
    libc::memory_clear(MENU + 0x60, 0x50);
    const int32_t option =
        static_cast<int32_t>(static_cast<uint32_t>(options_state().player_gender));
    if (option == 0) {
        menu_state().player_gender =
            static_cast<uint16_t>(random_next(game_state_block().object_24, 100) & 1);
    } else {
        menu_state().player_gender = static_cast<uint16_t>(option == 1 ? 0 : 1);
    }
    if (static_cast<uint32_t>(menu_state().game_mode) == 1) {
        menu_state().player_gender = static_cast<uint16_t>(0);
        player_record(1).gender = 1;
    }
}

// 0x18002758 — start the hole: the hole screen, the hole set up, the ball at its rest if the
// round is resumed, the turn recorded, the first-hole picture unless the save says otherwise,
// the slides that drop in, then the hole entered. A resumed hole also opens the pause menu.
void hole_start() {
    hole_begin();  // the ball's path is recorded from the tee of each hole (round_history.h)
    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    screen_set(static_cast<uint32_t>(static_cast<int8_t>(screen_state().id)));
    hole_setup();
    if (static_cast<uint32_t>(play_state().resuming) != 0) {
        course_resume();
        play_state().ball_x = player_record(0).ball_rest_x;
        play_state().ball_y = player_record(0).ball_rest_y;
    }
    ball_rest_record();
    bool picture_first = false;
    if (static_cast<uint32_t>(text.round_started) == 0) {
        text.round_started = static_cast<uint8_t>(1);
        if (static_cast<uint32_t>(screen_state().byte_d85) == 1 &&
            static_cast<uint32_t>(screen_state().byte_dd0) == 1) {
            picture_first = true;
        } else {
            play_state().state = 0x1a;  // PICTURE_START
        }
    }
    // The slides: the course picture dropping from the top, the title layer, then three more
    // pictures sliding in from the right.
    Slide* slides = text_block().slides;
    slides[0].picture = GAME_STATE + 0x84fb0;
    slides[0].x = 0;
    slides[0].y = to_fixed(SCREEN_HEIGHT);
    slides[0].speed = static_cast<uint32_t>(-static_cast<int64_t>(
        (static_cast<int64_t>(as_image(GAME_STATE + 0x84fb0).height) << 32) / (to_fixed(30))));
    slides[1].picture = GAME_STATE + 0x84fec;
    const ImageRecord& second = as_image(GAME_STATE + 0x84fec);
    slides[1].x = (SCREEN_WIDTH - second.width) << 16;
    slides[1].y = (second.origin_y + SCREEN_HEIGHT) << 16;
    slides[1].speed = static_cast<uint32_t>(
        -static_cast<int64_t>((static_cast<int64_t>(second.height) << 32) / (to_fixed(30))));
    for (uint32_t i = 0; i < 3; ++i) {
        Slide& slide = slides[i + 2];
        slide.picture = GAME_STATE + game_state::COURSE_TABLE + 0xa10 + (i + 0x1a) * image::SIZE;
        slide.x = slides[1].x;
        slide.y = slides[1].y;
        slide.speed = slides[1].speed;
    }
    text_block().word_738 = 2;
    text_block().word_734 = 0;
    text_block().word_73c = 0;
    hole_enter();
    if (static_cast<uint32_t>(play_state().byte_799) != 1 || !picture_first) {
        return;
    }
    play_state().byte_799 = static_cast<uint8_t>(0);
    pause_menu_enter();
    play_state().ticked = static_cast<uint8_t>(1);
}

// 0x18004a80 — ask for a course to load: every sound slot stopped and released, the files the
// last course owned freed, the course and hole noted, the loading flags raised, the state
// machine moved to its loading phase, the title image and defaults reloaded, and the loading
// screen set (a tail call).
void course_load_request(uint32_t course, uint32_t hole) {
    {
        for (uint32_t slot = 0; slot < 10; ++slot) {
            if (play_state().sound_enabled[slot] != 0) {
                sound_slot_stop(play_state().slot, slot);
            }
        }
        for (uint32_t slot = 0; slot < 10; ++slot) {
            if (play_state().sound_enabled[slot] != 0) {
                sound_slot_release(play_state().slot, slot);
                play_state().sound_enabled[slot] = 0;
            }
        }
        files_release(1);
        text_block().byte_745 = static_cast<uint8_t>(course);
        play_state().byte_7be = static_cast<uint8_t>(1);
        screen_state().save_fresh = static_cast<uint8_t>(1);
        play_state().byte_819 = static_cast<uint8_t>(0);
        play_state().byte_81a = static_cast<uint8_t>(hole);
        app2_state().phase = static_cast<uint8_t>(5);
        load_title_and_defaults();  // load_title_and_defaults
    }
    screen_set_tail(0xb);
}

// --- screen entry points ---------------------------------------------------------------------
// Each opens its screen and then forgets any input still pending, so a press that
// chose the screen cannot also act on it.

void hole_screen_enter() {
    hole_enter();
    wheel_slots_clear();
}

void hole_screen_tick(uint32_t milliseconds) {
    hole_tick(milliseconds);
}

}  // namespace minigolf::game
