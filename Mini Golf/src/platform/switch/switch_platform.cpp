// The Nintendo Switch platform: homebrew on HorizonOS, through libnx.
//
// The console draws with a plain linear framebuffer rather than deko3d or EGL, because the game
// hands over a finished 320×240 picture and nothing else: the whole video path is one nearest
// blit. 720 is exactly three times 240, so the picture is magnified by whole pixels with black
// bars either side — the same thing "Whole multiples only" does on the desktop. At a whole
// multiple every scaling mode draws the same pixels, so the Graphics settings would have nothing
// to choose between and are left alone here.
//
// The pad goes through the same portable binding table as the desktop's keyboard
// (platform/input_bindings.h), so the seven controls are rebindable in principle and the buttons
// this console offers are registered as the inputs a settings screen could show.
//
// Sound effects are in switch_audio.cpp: the console gives homebrew one PCM stream and no mixer,
// so there is one here. What is not here at all is the music (AAC in .m4a, and no decoder for it
// that is ours to use), the software keyboard for typing a name, and any way to choose a file —
// each of those is a `Platform` method that answers "no" rather than a stub that lies.
#include "gamedata/install.h"
#include "gamedata/manifest.h"
#include "platform/input_bindings.h"
#include "platform/paths.h"
#include "platform/platform.h"
#include "platform/switch/switch_audio.h"
#include "platform/switch/switch_settings.h"
#include "runtime/runtime.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <switch.h>
#include <vector>

namespace minigolf::platform {

namespace {

using minigolf::platform::Action;

// The console's screen, and the whole-number scale that fits the game's picture into it.
constexpr unsigned DISPLAY_WIDTH = 1280, DISPLAY_HEIGHT = 720;
constexpr unsigned SCALE = DISPLAY_HEIGHT / SCREEN_HEIGHT;  // 3
constexpr unsigned PICTURE_WIDTH = SCREEN_WIDTH * SCALE, PICTURE_HEIGHT = SCREEN_HEIGHT * SCALE;
constexpr unsigned PICTURE_LEFT = (DISPLAY_WIDTH - PICTURE_WIDTH) / 2;
constexpr unsigned PICTURE_TOP = (DISPLAY_HEIGHT - PICTURE_HEIGHT) / 2;

// The wheel keeps turning while a direction is held: the first repeat waits, the rest follow
// quickly. A keyboard's own auto-repeat does this on the desktop; a pad has none.
constexpr unsigned REPEAT_DELAY_FRAMES = 20, REPEAT_EVERY_FRAMES = 5;

// One detent short of a menu row is no use to anybody: the wheel moves a row at a time here, as
// a key press does on the desktop (see DETENTS_PER_ROW there).
constexpr int DETENTS_PER_ROW = 8;

// The buttons this console offers, in the order a settings screen should list them. The codes are
// libnx's own button bits, which is what `InputCode` is for: only this platform knows what they
// mean. HidNpadButton values are single bits below 1 << 26, so they fit an InputCode.
// Named PadButton, not Button: `platform::Button` is already the iPod's five, and two things of
// the same name in the same namespace is a trap for whoever reads this next.
struct PadButton {
    InputCode code;
    const char* label;
};
constexpr PadButton BUTTONS[] = {
    {HidNpadButton_A, "A"},
    {HidNpadButton_B, "B"},
    {HidNpadButton_X, "X"},
    {HidNpadButton_Y, "Y"},
    {HidNpadButton_L, "L"},
    {HidNpadButton_R, "R"},
    {HidNpadButton_ZL, "ZL"},
    {HidNpadButton_ZR, "ZR"},
    {HidNpadButton_Left, "D-Pad Left"},
    {HidNpadButton_Right, "D-Pad Right"},
    {HidNpadButton_Up, "D-Pad Up"},
    {HidNpadButton_Down, "D-Pad Down"},
    {HidNpadButton_StickLLeft, "Left Stick Left"},
    {HidNpadButton_StickLRight, "Left Stick Right"},
    {HidNpadButton_StickLUp, "Left Stick Up"},
    {HidNpadButton_StickLDown, "Left Stick Down"},
    {HidNpadButton_StickL, "Left Stick"},
    {HidNpadButton_StickR, "Right Stick"},
};
constexpr unsigned BUTTON_COUNT = sizeof BUTTONS / sizeof BUTTONS[0];

// The two buttons a player cannot bind, and so are not in the list above: Plus closes the game,
// the way homebrew is always closed, and Minus opens the settings screen. A player who bound
// either would have shut the door behind them.
constexpr uint64_t QUIT_BUTTON = HidNpadButton_Plus;
constexpr uint64_t SETTINGS_BUTTON = HidNpadButton_Minus;

const InputChoice* assignable_buttons() {
    static InputChoice choices[BUTTON_COUNT];
    for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
        choices[i] = InputChoice{BUTTONS[i].code, BUTTONS[i].label};
    }
    return choices;
}

// The one instance, so that the fatal handler below can reach the console it owns.
class SwitchPlatform;
SwitchPlatform* running = nullptr;

class SwitchPlatform final : public Platform {
public:
    explicit SwitchPlatform(const char* title) {
        // The console first: it is where anything that goes wrong before the first frame can be
        // said, and the game's own picture replaces it as soon as there is one.
        consoleInit(nullptr);
        std::printf("%s\n\nStarting...\n", title);
        consoleUpdate(nullptr);

        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad_);

        // The pad, as the seven controls. Two inputs each where a second is natural: the stick
        // turns the wheel as well as the D-pad.
        const InputCode defaults[ACTION_COUNT][BINDING_SLOTS] = {
            {HidNpadButton_Left, HidNpadButton_StickLLeft},    // SwipeLeft
            {HidNpadButton_Right, HidNpadButton_StickLRight},  // SwipeRight
            {HidNpadButton_A, NO_INPUT},                       // Select
            {HidNpadButton_X, NO_INPUT},                       // PlayPause
            {HidNpadButton_B, NO_INPUT},                       // Menu
            {HidNpadButton_L, NO_INPUT},                       // Rewind
            {HidNpadButton_R, NO_INPUT},                       // FastForward
        };
        set_default_bindings(defaults);
        set_assignable_inputs(assignable_buttons(), BUTTON_COUNT);

        audio_open();
        running = this;
        // Anything fatal goes to stderr, which on this console is a place with nobody in it. Put
        // it on screen instead and wait, or the program simply vanishes back to the menu.
        set_fatal_handler([](const char* message) {
            if (running != nullptr) {
                running->show_fatal(message);
            }
        });
    }

    ~SwitchPlatform() override {
        running = nullptr;
        set_fatal_handler(nullptr);
        audio_close();
        if (framebuffer_open_) {
            framebufferClose(&framebuffer_);
        }
        if (console_open_) {
            consoleExit(nullptr);
        }
    }

    SwitchPlatform(const SwitchPlatform&) = delete;
    SwitchPlatform& operator=(const SwitchPlatform&) = delete;

    void poll(FrameInput& input) override {
        input = FrameInput{};
        if (!appletMainLoop()) {  // the system is taking the applet away
            input.quit = true;
            return;
        }
        padUpdate(&pad_);
        const uint64_t down = padGetButtonsDown(&pad_);
        const uint64_t held = padGetButtons(&pad_);
        if ((down & QUIT_BUTTON) != 0) {
            input.quit = true;
            return;
        }
        if ((down & SETTINGS_BUTTON) != 0) {
            settings_screen();
            return;  // the frame the screen was opened on gives the game nothing
        }

        for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
            const InputCode code = BUTTONS[i].code;
            Action action = Action::Select;
            if (!input_bindings().action_for(code, action)) {
                continue;
            }
            const bool wheel = action == Action::SwipeLeft || action == Action::SwipeRight;
            if (!wheel) {
                if ((down & code) != 0) {
                    input.buttons |= button_for(action);
                }
                continue;
            }
            // A turn of the wheel on the press, and again while the button is held down.
            const bool repeat = (held & code) != 0 && repeat_due(i);
            if ((down & code) != 0 || repeat) {
                input.wheel_detents +=
                    action == Action::SwipeLeft ? -DETENTS_PER_ROW : DETENTS_PER_ROW;
            }
            if ((down & code) != 0) {
                held_frames_[i] = 0;
            }
        }
        for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
            held_frames_[i] = (held & BUTTONS[i].code) != 0 ? held_frames_[i] + 1 : 0;
        }
        audio_service();
    }

    void present(const uint8_t* rgb) override {
        if (!framebuffer_open_) {
            open_framebuffer();
        }
        uint32_t stride = 0;
        auto* pixels = static_cast<uint32_t*>(framebufferBegin(&framebuffer_, &stride));
        if (pixels == nullptr) {  // the system has taken the display away for a moment
            return;
        }
        const uint32_t words_per_row = stride / 4;
        // The bars either side of the picture, painted once a frame: cheaper than clearing the
        // whole screen, and nothing else ever writes there.
        for (unsigned y = 0; y < DISPLAY_HEIGHT; ++y) {
            uint32_t* row = pixels + y * words_per_row;
            std::memset(row, 0, PICTURE_LEFT * 4);
            std::memset(row + PICTURE_LEFT + PICTURE_WIDTH, 0,
                        (DISPLAY_WIDTH - PICTURE_LEFT - PICTURE_WIDTH) * 4);
        }
        for (unsigned y = 0; y < SCREEN_HEIGHT; ++y) {
            const uint8_t* source = rgb + y * SCREEN_WIDTH * 3;
            uint32_t* first = pixels + (PICTURE_TOP + y * SCALE) * words_per_row + PICTURE_LEFT;
            for (unsigned x = 0; x < SCREEN_WIDTH; ++x) {
                const uint32_t colour = static_cast<uint32_t>(source[x * 3]) |
                                        static_cast<uint32_t>(source[x * 3 + 1]) << 8 |
                                        static_cast<uint32_t>(source[x * 3 + 2]) << 16 |
                                        0xff000000u;
                for (unsigned across = 0; across < SCALE; ++across) {
                    first[x * SCALE + across] = colour;
                }
            }
            // The other rows of this source row are copies of the first.
            for (unsigned down = 1; down < SCALE; ++down) {
                std::memcpy(first + down * words_per_row, first, PICTURE_WIDTH * 4);
            }
        }
        framebufferEnd(&framebuffer_);
    }

    // The framebuffer is double-buffered against the display, so `framebufferEnd` has already
    // waited for the next flip: the console paces the game at its own 60 Hz. The frame rate in
    // Settings ▸ General is not offered here for the same reason.
    void wait_for_next_frame() override {}

    void play_sound(const std::string& wav_path, bool looping) override {
        audio_play(wav_path, looping);
    }

    void stop_sound(const std::string& wav_path) override { audio_stop(wav_path); }

    // The music is AAC in an .m4a, and this console has no decoder for it that is ours to use.
    void play_music(const std::string& /*path*/, bool /*repeat*/) override {}

    // There is no file browser to ask with. Rather than fail silently, say on screen where the
    // game's files must be put — this is called exactly when they are missing.
    bool choose_file(const std::string& prompt, const std::string& /*extension*/,
                     std::string& /*chosen_path*/) override {
        to_console();
        const std::string game_dir = data_directory() + "/" + gamedata::GAME_DIRECTORY_NAME;
        // What is wrong, in the words of the check itself. It goes to stderr as well, which on
        // this console is a place with nobody in it — and "copy the files" is no help at all when
        // the files are there and one of them is damaged.
        std::string why = "they are not there";
        (void)gamedata::verify_installed(game_dir, why);
        std::printf("\n%s\n\n"
                    "The game's own files cannot be used:\n\n    %s\n\n"
                    "Copy the folder \"%s\" from your iPod to\n\n    %s\n\n"
                    "on the SD card, then start this again.\n\n"
                    "Press + to quit.\n",
                    prompt.c_str(), why.c_str(), gamedata::GAME_DIRECTORY_NAME, game_dir.c_str());
        while (appletMainLoop()) {
            padUpdate(&pad_);
            if ((padGetButtonsDown(&pad_) & QUIT_BUTTON) != 0) {
                break;
            }
            consoleUpdate(nullptr);
        }
        return false;
    }

    // The controls, on the console: it and the game's picture are the same display, so the
    // display changes hands for as long as the screen is up.
    void settings_screen() {
        to_console();
        switch_settings_screen(pad_);
        open_framebuffer();
    }

    // Called from the fatal handler: the message, and a chance to read it.
    void show_fatal(const char* message) {
        to_console();
        std::printf("\n\nSomething went wrong and the game has stopped:\n\n    %s\n\n"
                    "Press + to return to the menu.\n",
                    message);
        while (appletMainLoop()) {
            padUpdate(&pad_);
            if ((padGetButtonsDown(&pad_) & QUIT_BUTTON) != 0) {
                break;
            }
            consoleUpdate(nullptr);
        }
    }

private:
    static uint32_t button_for(Action action) {
        switch (action) {
        case Action::Select:
            return static_cast<uint32_t>(platform::Button::Select);
        case Action::PlayPause:
            return static_cast<uint32_t>(platform::Button::Play);
        case Action::Menu:
            return static_cast<uint32_t>(platform::Button::Menu);
        case Action::Rewind:
            return static_cast<uint32_t>(platform::Button::Previous);
        case Action::FastForward:
            return static_cast<uint32_t>(platform::Button::Next);
        default:
            return 0;
        }
    }

    // Whether a held button has waited long enough for its next turn of the wheel.
    [[nodiscard]] bool repeat_due(unsigned index) const {
        const unsigned frames = held_frames_[index];
        return frames >= REPEAT_DELAY_FRAMES &&
               (frames - REPEAT_DELAY_FRAMES) % REPEAT_EVERY_FRAMES == 0;
    }

    void open_framebuffer() {
        if (console_open_) {
            consoleExit(nullptr);
            console_open_ = false;
        }
        framebufferCreate(&framebuffer_, nwindowGetDefault(), DISPLAY_WIDTH, DISPLAY_HEIGHT,
                          PIXEL_FORMAT_RGBA_8888, 2);
        framebufferMakeLinear(&framebuffer_);
        framebuffer_open_ = true;
    }

    void to_console() {
        if (framebuffer_open_) {
            framebufferClose(&framebuffer_);
            framebuffer_open_ = false;
        }
        if (!console_open_) {
            consoleInit(nullptr);
            console_open_ = true;
        }
    }

    PadState pad_{};
    Framebuffer framebuffer_{};
    bool framebuffer_open_ = false;
    bool console_open_ = true;
    unsigned held_frames_[BUTTON_COUNT] = {};
};

}  // namespace

std::unique_ptr<Platform> create_platform(const char* window_title,
                                          unsigned /*frames_per_second*/) {
    return std::make_unique<SwitchPlatform>(window_title);
}

}  // namespace minigolf::platform
