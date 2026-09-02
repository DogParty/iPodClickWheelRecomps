// The headless platform: no window, no input, no pacing. Used by the oracle tests and CI, where
// the game is driven entirely by a script and the frames are never looked at.
#include "platform/platform.h"

namespace holdem::platform {

namespace {

class NullPlatform final : public Platform {
public:
    void poll(FrameInput& input) override { input = FrameInput{}; }
    void present(const uint8_t* /*rgb*/, unsigned /*width*/, unsigned /*height*/) override {}
    void wait_for_next_frame() override {}
    void play_sound(uint32_t /*voice*/, const SoundClip& /*clip*/, bool /*looping*/) override {}
    void stop_sound(uint32_t /*voice*/) override {}
    void play_music(const std::string& /*path*/, bool /*repeat*/) override {}
    bool choose_file(const std::string& /*prompt*/, const std::string& /*extension*/,
                     std::string& /*chosen_path*/) override {
        return false;
    }
};

}  // namespace

std::unique_ptr<Platform> create_platform(const char* /*window_title*/,
                                          unsigned /*frames_per_second*/) {
    return std::make_unique<NullPlatform>();
}

}  // namespace holdem::platform
