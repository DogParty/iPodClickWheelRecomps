// The music: which track plays on the menus and in each course, under the music option.
#include "calling.h"
#include "framework/audio.h"
#include "framework/device.h"
#include "game_state.h"
#include "records.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"

namespace minigolf::game {

namespace {

constexpr uint32_t TITLE_SOUNDS = 0x1801'9a48, TITLE_SOUND_COUNT = 6;  // pairs of words
constexpr uint32_t MUSIC_OFF = 0, MUSIC_AUTO = 2;
constexpr uint32_t CURRENT_TRACK = MENU + 0x2b;  // signed byte (= TEXT + 0x72b)

}  // namespace

void stream_register(uint32_t name, uint32_t stream);  // 0x18014ba4, below

// 0x18004860 — start the menu track (0) or the current course's track (1), unless the music is
// off, set to follow the iPod's own (and something is playing), or already that track. The
// first time, the title's audio is let go. Each course has a menu and an in-game track.
void music_start(uint32_t in_game) {
    bool resume = false;
    {
        if (in_game >= 2) {
            assert_trap(0x18004870u);
        }
        const int32_t option = static_cast<int32_t>(static_cast<uint32_t>(options_state().music));
        if (option == static_cast<int32_t>(MUSIC_OFF)) {
            return;
        }
        if (option == static_cast<int32_t>(MUSIC_AUTO) &&
            static_cast<uint32_t>(play_state().audio_flag) != 0) {
            return;
        }
        if (static_cast<uint32_t>(guest<int8_t>(CURRENT_TRACK)) == in_game) {
            return;
        }
        guest<int8_t>(CURRENT_TRACK) = static_cast<int8_t>(in_game);
        play_state().audio_flag = static_cast<uint8_t>(0);
        audio::stop_music();
        if (static_cast<uint32_t>(app2_state().title_loaded) != 0) {
            (void)audio::stream_finished();
            for (uint32_t i = 0; i < TITLE_SOUND_COUNT; ++i) {
                stream_register(ld32(TITLE_SOUNDS + i * 8),
                                guest_array<uint32_t>(TITLE_SOUNDS)[i * 2 + 1]);
            }
            app2_state().title_loaded = static_cast<uint8_t>(0);
        }
        (void)audio::engine_reset();  // 0x18013588; the answer was always 0x7fff
        const int32_t course = static_cast<int32_t>(menu_state().course);
        if (course < 0 || course > 2) {
            return;
        }
        // Tracks: course 0 menu 1 / game 0; course 1 menu 3 / game 2; course 2 menu 5 / game 4.
        const uint32_t tracks[3][2] = {{1, 0}, {3, 2}, {5, 4}};
        audio::play_stream(tracks[course][in_game]);
        resume = true;
    }
    if (resume) {  // a tail call in the original: the audio resumes with the caller's return
        audio::set_repeat_mode(1);
    }
}

// 0x18014ba4 — register an audio stream with the framework under the path the framework
// resolves for `name` (misc #14 writes up to 0x200 bytes of it).
void stream_register(uint32_t name, uint32_t stream) {
    GuestScratch frame(4 * 2 + 0x218);
    guest<uint32_t>(frame.at(0x14)) = 0x200;
    device::resolve_resource(0, frame.at(0x18), frame.at(0x14), name);
    guest<uint8_t>(frame.at(0xc)) = static_cast<uint8_t>(0);
    (void)audio::register_stream(frame.at(0x18), 0, guest<uint32_t>(frame.at(0xc)), stream);
}

}  // namespace minigolf::game
