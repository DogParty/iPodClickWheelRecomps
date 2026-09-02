// What the game needs from the machine it runs on: a screen, a way to wait for the next frame,
// and the click wheel. Each platform directory provides one implementation and the function
// that creates it; everything above this line is portable.
//
// The iPod's input model is deliberately preserved rather than abstracted into "keys": the
// wheel reports detents turned, and the five buttons are the five bits the game tests.
#pragma once

#include "platform/save_store.h"
#include "platform/settings.h"
#include "platform/text_entry.h"

#include <cstdint>
#include <memory>
#include <string>

namespace minigolf::platform {

constexpr unsigned SCREEN_WIDTH = 320;
constexpr unsigned SCREEN_HEIGHT = 240;

// The loudest `set_audio_level` is told. The scale is the device's own (the game asks the
// framework what it is and works in that), and the runtime hands it over unchanged.
constexpr unsigned AUDIO_LEVEL_MAX = 255;

// The five click-wheel buttons as the flag bits the game reads (see runtime/main.cpp for where
// they land). 0x10 was measured to open the pause menu; the other assignments follow the
// emulator's key map and are not individually verified against hardware.
enum class Button : uint32_t {
    Select = 0x01,
    Previous = 0x02,
    Play = 0x04,
    Next = 0x08,
    Menu = 0x10,
};

struct FrameInput {
    int wheel_detents = 0;    // net detents since the last poll; positive is clockwise
    uint32_t buttons = 0;     // Button bits pressed since the last poll (edge, not level)
    bool quit = false;        // the user closed the window or asked to stop
    bool screenshot = false;  // the user asked for the framebuffer to be saved
    TypedText typed;          // what they typed, where the platform offers typing at all
};

class Platform {
public:
    virtual ~Platform() = default;

    // Collect everything that happened since the previous call.
    virtual void poll(FrameInput& input) = 0;

    // Show a frame: packed 24-bit RGB, `width` x `height`, top row first.
    //
    // The size is passed rather than assumed because the renderer may be drawing at a multiple of
    // the game's own 320x240 (gfx::set_render_scale). It is always that shape, so a platform that
    // fits the picture to its window has nothing extra to do; what it must not do is take the
    // pitch from SCREEN_WIDTH.
    virtual void present(const uint8_t* rgb, unsigned width, unsigned height) = 0;

    // Block until the next frame is due (frame pacing lives here, not in the game loop).
    virtual void wait_for_next_frame() = 0;

    // Sound effects: `.wav` files by absolute path. Re-triggering a sound that is still playing
    // restarts it; the device had one voice per sound.
    virtual void play_sound(const std::string& wav_path, bool looping) = 0;
    virtual void stop_sound(const std::string& wav_path) = 0;

    // Music: one track at a time, a second request replaces the first. The files are AAC `.m4a`.
    virtual void play_music(const std::string& path, bool repeat) = 0;

    // Silence the music until another track is asked for. Turning Music off in the game's
    // Options is this and nothing else, so a platform that leaves it unimplemented plays on
    // after the player has said to stop.
    virtual void stop_music() {}

    // The device's output level, 0..AUDIO_LEVEL_MAX, as the game's Volume page set it. The
    // iPod's volume belonged to the device rather than to one part of the game, so this applies
    // to the music and the sound effects together. Called whenever it changes.
    virtual void set_audio_level(unsigned /*level*/) {}

    // Ask the player for a file with the native file browser. Blocks until they choose or
    // cancel; false when cancelled or when the platform has no way to ask (headless).
    virtual bool choose_file(const std::string& prompt, const std::string& extension,
                             std::string& chosen_path) = 0;

    // Whether this machine offers typing (platform/text_entry.h). A platform that says yes
    // fills `FrameInput::typed`; one that says no leaves it empty and the wheel is the only way
    // to spell a name.
    [[nodiscard]] virtual bool text_input_supported() const { return false; }

    // Where saved games belong on this machine. A platform with somewhere of its own — a save
    // archive, an app's private storage — returns a store for it; returning nothing accepts the
    // default, which is one file per save beside the game, as the iPod did.
    virtual std::unique_ptr<SaveStore> create_save_store() { return nullptr; }

    // Take `settings()` as it now stands: called once the saved settings have been read, and
    // again whenever something changes them. A platform that has nothing to apply ignores it.
    virtual void apply_settings() {}
};

// Provided by whichever platform directory is linked in.
std::unique_ptr<Platform> create_platform(const char* window_title, unsigned frames_per_second);

}  // namespace minigolf::platform
