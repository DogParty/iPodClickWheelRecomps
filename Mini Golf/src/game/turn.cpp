// Whose turn it is: the ball each player left behind, and who putts next.
//
// Each player record (MENU + player * PLAYER_STRIDE) keeps the ball's position and aim between
// turns and a RESULT: 0 before the first putt, 1 while the hole is in play, 2 once holed and
// negative after the stroke limit. In pass 'n play the two players alternate; the others
// always put player 0 back.
#include "turn.h"

#include "calling.h"
#include "course.h"
#include "game_state.h"
#include "hole_tick.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"

namespace minigolf::game {

namespace {

enum : uint32_t { RESULT_FRESH = 0, RESULT_PLAYING = 1, RESULT_HOLED = 2 };

uint32_t current_player() {
    return static_cast<uint32_t>(players_state().current);
}
PlayerRecord& record(uint32_t player) {
    return player_record(player);
}
int32_t result(uint32_t player) {
    return static_cast<int16_t>(record(player).result);
}

void ball_from_record(uint32_t player) {
    play_state().ball_x = record(player).ball_rest_x;
    play_state().ball_y = record(player).ball_rest_y;
    play_state().aim_angle = record(player).ball_rest_angle;
}

}  // namespace

// 0x18011788 — the current player takes up their ball: placing it if they have not putted yet,
// aiming otherwise. A holed or limited result is cleared first (the next hole's state).
void turn_resume() {
    const uint32_t player = current_player();
    uint32_t state = PLACING;
    if (result(player) == RESULT_PLAYING) {
        state = AIMING;
    } else if (result(player) != RESULT_FRESH) {
        record(player).result = static_cast<uint16_t>(RESULT_FRESH);
    }
    play_state().state = state;
    ball_from_record(player);
}

// 0x1800db70 — the ball has come to rest: remember where for the current player, then decide
// who plays next. Pass 'n play hands over to the other player (or opens the score card once
// both have holed); the other modes keep player 0 and go back to placing or aiming.
void ball_rest_record() {
    const uint32_t player = current_player();
    record(player).ball_rest_x = play_state().ball_x;
    record(player).ball_rest_y = play_state().ball_y;
    record(player).ball_rest_angle = play_state().aim_angle;
    if (result(player) == RESULT_FRESH) {
        record(player).result = static_cast<uint16_t>(RESULT_PLAYING);
    } else if (result(player) < 0) {
        record(player).result = static_cast<uint16_t>(RESULT_FRESH);
    }

    TextBlock& text = as_text(GAME_STATE + game_state::TEXT);
    const int32_t mode = static_cast<int32_t>(static_cast<int8_t>(menu_state().game_mode));
    if (mode == MODE_PASS_N_PLAY) {
        if (player != 0) {
            if (result(1) == RESULT_HOLED) {
                text.score_card_flag = static_cast<uint8_t>(0);
                score_card_open();
            } else {
                turn_resume();
            }
        } else if (result(0) == RESULT_HOLED) {
            players_state().current = static_cast<int16_t>(1);
            play_state().state = TURN_MESSAGE;
        } else if (record(0).strokes != 0) {
            turn_resume();
        } else {
            record(0).result = static_cast<uint16_t>(RESULT_FRESH);
            play_state().state = TURN_MESSAGE;
        }
    } else {
        players_state().current = static_cast<int16_t>(0);
        ball_from_record(0);
        if (record(0).strokes == 0) {
            record(0).result = static_cast<uint16_t>(RESULT_FRESH);
            play_state().state = PLACING;
        } else if (result(0) <= 0) {
            play_state().state = PLACING;
        } else if (result(0) != RESULT_HOLED) {
            play_state().state = AIMING;
        } else if (mode == 0) {  // single player: holed, on to the card
            text.score_card_flag = static_cast<uint8_t>(0);
            score_card_open();
        }
    }
    ball_from_record(current_player());
}

}  // namespace minigolf::game
