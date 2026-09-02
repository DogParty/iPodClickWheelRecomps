// The Audio framework's side effects (src/libeapp/audio.cpp): the requests it queues for the
// platform, and the device level it keeps.
//
// Three of these calls used to only write themselves to the call log and do nothing else, and
// each was a fault a player could hear:
//
//   * #45 stop music. Turning Music off in Options is this call and nothing else (menu.cpp), so
//     the track played on regardless of the setting.
//   * #53/#51 the device level. The Volume page walks a number up and down and hands it here;
//     with nothing keeping it, the slider moved and the sound did not — and #51 answering zero
//     meant the page drew an empty bar every time it opened, whatever the volume really was.
//   * #5 stop sound, which left anything already sounding to finish.
//
// The oracle cannot catch any of that: it compares the calls the game *makes*, and the game was
// making the right ones. What was missing was on this side of them.
#include "framework/audio.h"
#include "ipod_eapp.h"

#include <cstdio>
#include <string>

namespace {

using namespace minigolf;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Anything a previous check left queued, thrown away.
void drain() {
    (void)eapp::take_sound_requests();
    (void)eapp::take_music_requests();
}

void test_stop_music_is_a_request() {
    drain();
    audio::stop_music();
    const std::vector<eapp::MusicRequest> requests = eapp::take_music_requests();
    check(requests.size() == 1, "stopping the music asks the platform to stop it");
    if (requests.size() == 1) {
        check(requests[0].action == eapp::MusicAction::Stop, "and the request says stop");
    }
    check(eapp::take_music_requests().empty(), "and it is taken only once");
}

void test_level_is_kept_and_reported() {
    // Full at start-up, which is what the hardware reported for a player who had not turned it
    // down — `init.cpp` reads it straight back to place the slider.
    check(audio::music_level() == eapp::AUDIO_LEVEL_MAX, "the device starts at full volume");
    check(eapp::audio_level() == eapp::AUDIO_LEVEL_MAX, "and the platform is told the same");

    audio::set_music_level(0x40);
    check(audio::music_level() == 0x40, "a level set is a level read back");
    check(eapp::audio_level() == 0x40, "and the platform sees it");

    audio::set_music_level(0);
    check(audio::music_level() == 0, "silence is a level like any other");
    check(eapp::audio_level() == 0, "and reaches the platform");

    // The game divides by the scale it was given, so it cannot ask for more than the scale —
    // but a level that arrived larger would be a gain above 1, which clips.
    audio::set_music_level(eapp::AUDIO_LEVEL_MAX + 100);
    check(audio::music_level() == eapp::AUDIO_LEVEL_MAX, "a level past full is held at full");

    check(audio::music_level_scale() == eapp::AUDIO_LEVEL_MAX,
          "the scale the game is told to work in is the one the level is kept in");
    audio::set_music_level(eapp::AUDIO_LEVEL_MAX);  // leave it as the next test expects
}

// The level survives being read: the Volume page reads it every frame it is open.
void test_reading_the_level_does_not_change_it() {
    audio::set_music_level(0x20);
    for (int i = 0; i < 5; ++i) {
        check(audio::music_level() == 0x20, "reading the level leaves it alone");
    }
    audio::set_music_level(eapp::AUDIO_LEVEL_MAX);
}

void test_stop_sound_is_a_request() {
    drain();
    // A handle with no file behind it has nothing to stop, and must not queue a request naming
    // one: the platform matches requests by path.
    audio::stop_sound(999);
    check(eapp::take_sound_requests().empty(),
          "stopping a sound that was never loaded does nothing");
}

}  // namespace

int main() {
    test_stop_music_is_a_request();
    test_level_is_kept_and_reported();
    test_reading_the_level_does_not_change_it();
    test_stop_sound_is_a_request();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("audio: all checks passed\n");
    return 0;
}
