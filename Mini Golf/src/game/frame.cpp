// A frame's drawing, top down: the running screen's renderer (or the hole's loading screen),
// the clock and battery in the corner when the option asks for them, a menu sound held back
// until its frame comes, then the batcher flushed to OpenGL.
#include "frame.h"

#include "calling.h"
#include "draw.h"
#include "fixed.h"
#include "framework/device.h"
#include "game_state.h"
#include "libc.h"
#include "menu.h"
#include "resources.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "screens.h"
#include "sounds.h"
#include "state.h"
#include "strings.h"

#include <tuple>

namespace minigolf::game {

void sound_effect_play(uint32_t slot, uint32_t index, uint32_t volume, uint32_t rate, uint32_t pan);

void batcher_flush();
void slider_draw(int32_t y, int32_t level);
int32_t sine_degrees(int32_t degrees);

namespace {

// 0x18011400, the voice every sound plays on: the hook only ever answered this.
constexpr uint32_t VOICE = 0x7fff;

constexpr uint32_t FULL = 0x10000;
constexpr uint32_t CLOCK_FORMAT = 0x1800'ecc4;  // "%d:%02d"
constexpr uint32_t BATTERY_IMAGE = GAME_STATE + 0x84bb4;
constexpr int32_t BATTERY_X = 0x130, BATTERY_WIDTH = 0xf, BATTERY_HEIGHT = 9, BATTERY_FILL_U = 0x10;
constexpr uint32_t TEXT_LOADING = 0x75;
constexpr uint32_t LOADING_TITLE_IMAGE = GAME_STATE + 0x84e0c,
                   LOADING_PICTURE_IMAGE = GAME_STATE + 0x84e48;
constexpr int32_t LOADING_TITLE_Y = 0x14, LOADING_PICTURE_Y = 0xaa, LOADING_BAR_Y = 0x6e;
constexpr uint32_t BACKGROUND_IMAGE = GAME_STATE + game_state::BLOCK_84CE0;
constexpr uint32_t BACKGROUND_READY = GAME_STATE + 0x85ce0;  // negative until it is loaded
constexpr uint32_t RIPPLE_PHASE = GAME_STATE + game_state::SETTINGS + 0x5b4, RIPPLE_STEP = 8;
constexpr int32_t WAVE_HEIGHT = 10;

uint32_t handle() {
    return game_state_block().handle;
}

// The time in the top-left corner and the battery beside it.
void clock_battery_draw(const GuestScratch& frame) {
    const uint32_t time = frame.at(0x1c), text = frame.at(0x34);
    guest<uint8_t>(text) = static_cast<uint8_t>(0);
    // The answer is ignored, as the ARM ignores it: this game prints whatever the firmware left
    // in the buffer and has no fallback for a clock that is not there. Other titles do check it
    // (framework/device.h, `wall_clock`).
    static_cast<void>(device::wall_clock(time));
    libc::format_text(text, CLOCK_FORMAT, {guest<uint32_t>(time + 8), guest<uint32_t>(time + 4)});
    text_draw_at(handle(), as_font(screen_state().small_font), text, 1, 1, Align::Left,
                 static_cast<uint32_t>(Blend::Text));
    image_draw(BATTERY_X, 1, BATTERY_WIDTH, BATTERY_HEIGHT, as_image(BATTERY_IMAGE), 0, 0,
               static_cast<uint32_t>(as_image(BATTERY_IMAGE).variant), Blend::Text);
    int32_t percent =
        static_cast<int32_t>(battery_query(frame.at(0x44), frame.at(0x40), frame.at(0x3c)));
    if (percent > 100) {
        percent = 100;
    }
    const libc::Division division = libc::signed_divide(static_cast<uint32_t>(percent), 10);
    image_draw(BATTERY_X + 2, 3, division.quotient, 5, as_image(BATTERY_IMAGE), BATTERY_FILL_U, 2,
               static_cast<uint32_t>(as_image(BATTERY_IMAGE).variant), Blend::Text);
}

}  // namespace

// 0x18011de8 — the loading screen. While a course loads: black, the two loading images and a
// bar for the files done so far. Before that (the start-up load): the background when it is
// in, a dim over it, and "Loading" with its letters riding a wave.
void loading_render() {
    GuestScratch frame(4 * 9 + 0x1c);
    if (play_state().byte_7be == 1) {
        rect_fill(0, 0, to_fixed(SCREEN_WIDTH), to_fixed(SCREEN_HEIGHT), 0, 0, 0, FULL,
                  Blend::Opaque);
        for (const auto& [image, y, return_address] :
             {std::tuple{LOADING_TITLE_IMAGE, LOADING_TITLE_Y, 0x18011e7cu},
              std::tuple{LOADING_PICTURE_IMAGE, LOADING_PICTURE_Y, 0x18011ec0u}}) {
            const uint32_t width = as_image(image).width;
            image_draw((static_cast<int32_t>(SCREEN_WIDTH - width)) / 2, y, width,
                       as_image(image).height, as_image(image), 0, 0, 1, Blend::KeyedAlt);
        }
        int32_t progress = 0;
        const int32_t files = static_cast<int32_t>(app2_state().word_08);
        if (files > 1) {
            progress = static_cast<int32_t>(libc::signed_divide(app2_state().score_entry * 100,
                                                                static_cast<uint32_t>(files - 1))
                                                .quotient);
            if (progress > 100) {
                progress = 100;
            }
        }
        slider_draw(LOADING_BAR_Y, progress);
        return;
    }
    uint32_t dim = 0xff;
    if (static_cast<int32_t>(guest<uint32_t>(BACKGROUND_READY)) >= 0) {
        image_draw(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, as_image(BACKGROUND_IMAGE), 0, 0, 0,
                   Blend::Opaque);
        dim = 0x80;
    }
    rect_fill(0, 0, to_fixed(SCREEN_WIDTH), to_fixed(SCREEN_HEIGHT), 0, 0, 0, dim << 8,
              Blend::Alpha);
    resource_load(as_pack(game_state_block().pack_handle), TEXT_LOADING, SCRATCH_TEXT, 0x800);
    const bool wide = static_cast<uint32_t>(menu_state().language) == LANGUAGE_WIDE;
    const uint32_t letter = frame.at(wide ? 0x10 : 0x14);  // one glyph, terminated
    const int32_t length =
        static_cast<int32_t>(wide ? wide_string_length(SCRATCH_TEXT) : string_length(SCRATCH_TEXT));
    if (wide) {
        guest<uint16_t>(letter + 2) = static_cast<uint16_t>(0);
    } else {
        guest<uint8_t>(letter + 1) = static_cast<uint8_t>(0);
    }
    FontRecord& font = as_font(screen_state().font_object);
    int32_t x = (static_cast<int32_t>(SCREEN_WIDTH) -
                 static_cast<int32_t>(text_width(font, SCRATCH_TEXT))) /
                2;
    uint32_t phase = guest<uint32_t>(RIPPLE_PHASE) + RIPPLE_STEP;
    guest<uint32_t>(RIPPLE_PHASE) = phase;
    for (int32_t i = 0; i < length; ++i, phase += RIPPLE_STEP) {
        const int64_t wave =
            static_cast<int64_t>(sine_degrees(static_cast<int32_t>(to_fixed(phase)))) *
            (to_fixed(WAVE_HEIGHT));
        const int32_t y =
            (static_cast<int32_t>(SCREEN_HEIGHT) - static_cast<int32_t>(font.line_height)) / 2 +
            static_cast<int32_t>(wave >> 32);
        if (wide) {
            guest<uint16_t>(letter) =
                static_cast<uint16_t>(guest<uint16_t>(SCRATCH_TEXT + static_cast<uint32_t>(i) * 2));
        } else {
            guest<uint8_t>(letter) =
                static_cast<uint8_t>(guest<uint8_t>(SCRATCH_TEXT + static_cast<uint32_t>(i)));
        }
        glyph_draw_at(handle(), font, letter, x, y);
        x += static_cast<int32_t>(text_width(font, letter));
    }
}

// 0x1800ead8 — the frame's drawing. Nothing while the app is leaving or until the tick has
// run; otherwise the screen's renderer (the loading screen while a course loads), the clock and
// battery if wanted, the held-back menu sound, and the batcher flushed. Returns what the
// renderer returned (1 from the loading screen).
uint32_t frame_render() {
    GuestScratch frame(4 * 8 + 0x48);
    if (static_cast<uint32_t>(app2_state().exiting) != 0 ||
        static_cast<uint32_t>(play_state().ticked) == 0) {
        return 1;
    }
    uint32_t result;
    if (play_state().byte_7be == 0) {
        const ScreenRender render = current_screen().render;
        if (render == nullptr) {
            assert_trap(0x1800eb38u);
        }
        result = render();
        if (static_cast<uint32_t>(save_data_byte(0x4d)) != 0 &&
            static_cast<uint32_t>(options_state().clock_battery) == 1) {
            clock_battery_draw(frame);
        }
    } else {
        loading_render();
        result = 1;
    }
    // Taken by address: a reference to a field of a packed overlay is not portable.
    uint32_t& countdown =
        guest<uint32_t>(PLAY + static_cast<uint32_t>(offsetof(PlayState, word_7e8)));
    if (static_cast<uint32_t>(options_state().sound_fx) == 0) {
        countdown = 0;
    } else if (countdown != 0) {
        countdown = countdown - 1;
        if (countdown == 0) {
            const uint32_t zero = device::clock_reserved();
            const uint32_t thousand = device::clock_rate();
            sound_effect_play(play_state().slot, play_state().word_7d8, VOICE, thousand, zero);
        }
    }
    batcher_flush();
    return result;
}

}  // namespace minigolf::game
