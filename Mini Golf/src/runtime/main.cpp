// The program: load the game, run its init vectors, then pump frames until told to stop.
//
// This is the recomp's equivalent of the iPod firmware's eApp task, and it copies the emulator's
// frame pump (reference/eapp-loader/play.rs) step for step, because the verification oracle
// compares the two runs call for call. Where the emulator's behaviour was a measured fact about
// the firmware it is cited; where it was the emulator's own convention (scratch allocations,
// the order of input delivery within a frame) it is copied anyway, since the recorded logs
// depend on it.
//
//   minigolf          [--script=FILE] [--fps=N]
//   minigolf-headless <image.bin> --gamedir=DIR [--script=FILE] [--call-log=FILE] [--frames=N]
//
// Without --gamedir the game's files are looked for in the platform's data directory
// (platform/paths.h) and, the first time, installed there from a zip the player picks
// (gamedata/install.h). With it — the oracle tests — the directory is used as given.
#include "framework/graphics.h"
#include "framework/storage.h"
#include "game/cheats.h"
#include "game/host_text.h"
#include "game/round_history.h"
#include "gamedata/install.h"
#include "gamedata/manifest.h"
#include "ipod_eapp.h"
#include "libeapp/heap.h"
#include "platform/input_bindings.h"
#include "platform/paths.h"
#include "platform/platform.h"
#include "platform/settings.h"
#include "platform/text_entry.h"
#include "runtime/cpu.h"
#include "runtime/eapp_image.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace minigolf {
namespace {

using platform::Button;

// ---------------------------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------------------------

struct Options {
    std::string image_path;     // defaults to the image inside game_dir
    std::string game_dir;       // the title's directory; empty = find or install it
    std::string script_path;    // scripted input, FRAME: ACTION per line
    std::string call_log_path;  // where to write the framework-call log
    std::string install_zip;    // install the game's files from this zip before running
    std::string fixed_time;     // HH:MM shown by the game's clock, for reproducible runs
    unsigned frame_limit = 0;   // stop after this many frames; 0 = run until quit
    // Behave as the emulator's own harness did — no button press times, and no quitting when the
    // game suspends itself. Only the oracle wants this; see tests/diff.sh.
    bool emulator_firmware = false;
    // Draw only what the iPod's game drew — no Cheats row, no extra statistics. What
    // --emulator-firmware needs for the recorded logs, and what tests/vs-recomp.sh gives the
    // decompiled build so it and the pure recompilation are drawing the same game
    // (game/host_text.h). Playing with it only takes features away.
    bool no_port_additions = false;
    unsigned frames_per_second =
        60;  // the game's own timebase (miscTBD #9 advances 1/60 s per call)
    bool frames_per_second_given = false;  // --fps= was asked for, so it beats the saved rate
    unsigned render_scale = 0;             // 0 = whatever the settings say
    unsigned render_threads = 0;           // 0 = one per core; 1 pins it to the calling thread
};

[[noreturn]] void usage(const char* program) {
    std::fprintf(
        stderr,
        "usage: %s [image.bin] [--gamedir=DIR] [--install-zip=FILE] [--script=FILE] "
        "[--call-log=FILE] [--frames=N] [--fps=N] [--time=HH:MM] [--no-port-additions] "
        "[--render-scale=1..8] [--render-threads=N] "
        "[--trace-entry=ADDR,...] [--dump-entry=ADDR:START:BYTES] [--dump-frame=START:BYTES]\n",
        program);
    std::exit(EXIT_FAILURE);
}

struct FrameDump {
    uint32_t start, bytes, from_frame;
};
std::vector<FrameDump>& frame_dumps() {
    static std::vector<FrameDump> dumps;
    return dumps;
}

void report_frame_dumps(unsigned frame) {
    for (const FrameDump& dump : frame_dumps()) {
        if (frame < dump.from_frame) {
            continue;
        }
        std::printf("frame %u at %08x:", frame, dump.start);
        for (uint32_t offset = 0; offset < dump.bytes; offset += 4) {
            std::printf("%s%08x", offset % 32 == 0 ? "\n  " : " ", ld32(dump.start + offset));
        }
        std::printf("\n");
    }
}

Options parse_options(int argc, char** argv) {
    Options options;
    const std::pair<const char*, std::string*> text_flags[] = {
        {"--gamedir=", &options.game_dir},       {"--script=", &options.script_path},
        {"--call-log=", &options.call_log_path}, {"--install-zip=", &options.install_zip},
        {"--time=", &options.fixed_time},
    };
    const std::pair<const char*, unsigned*> number_flags[] = {
        {"--frames=", &options.frame_limit},
        {"--fps=", &options.frames_per_second},
        // 0 leaves each at what the settings say: the scale the picture is drawn at, and how many
        // threads draw it. Neither changes what is drawn — see framework/graphics.h.
        {"--render-scale=", &options.render_scale},
        {"--render-threads=", &options.render_threads},
    };
    // --trace-entry=ADDR[,ADDR...]: print the registers every time a recompiled function at one
    // of these addresses is entered (compare with the emulator's `play --watch-pc`).
    const auto watch_entries = [](const std::string& list) {
        size_t start = 0;
        while (start < list.size()) {
            const size_t comma = list.find(',', start);
            const std::string item =
                list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            trace_entry_watch(static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 16)));
            start = comma == std::string::npos ? list.size() : comma + 1;
        }
    };
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        bool recognised = false;
        for (const auto& [flag, target] : text_flags) {
            if (argument.rfind(flag, 0) == 0) {
                *target = argument.substr(std::strlen(flag));
                recognised = true;
            }
        }
        for (const auto& [flag, target] : number_flags) {
            if (argument.rfind(flag, 0) == 0) {
                *target = static_cast<unsigned>(
                    std::strtoul(argument.c_str() + std::strlen(flag), nullptr, 10));
                options.frames_per_second_given |= target == &options.frames_per_second;
                recognised = true;
            }
        }
        if (argument.rfind("--trace-entry=", 0) == 0) {
            watch_entries(argument.substr(std::strlen("--trace-entry=")));
            recognised = true;
        }
        if (argument == "--emulator-firmware") {
            options.emulator_firmware = true;
            recognised = true;
        }
        if (argument == "--no-port-additions") {
            options.no_port_additions = true;
            recognised = true;
        }
        if (argument.rfind("--dump-frame=", 0) == 0) {
            // --dump-frame=START:BYTES (hex): memory printed after every frame, for comparing
            // two builds' state frame by frame.
            // --dump-frame=START:BYTES[:FROM] (hex): memory printed after every frame from FROM on.
            const std::string spec = argument.substr(std::strlen("--dump-frame="));
            const size_t colon = spec.find(':');
            if (colon != std::string::npos) {
                const size_t second = spec.find(':', colon + 1);
                frame_dumps().push_back(
                    {static_cast<uint32_t>(std::strtoul(spec.c_str(), nullptr, 16)),
                     static_cast<uint32_t>(std::strtoul(spec.c_str() + colon + 1, nullptr, 16)),
                     second == std::string::npos ? 0u
                                                 : static_cast<uint32_t>(std::strtoul(
                                                       spec.c_str() + second + 1, nullptr, 16))});
            }
            recognised = true;
        }
        if (argument.rfind("--dump-entry=", 0) == 0) {
            // --dump-entry=ADDR:START:BYTES (hex): memory printed at each entry to ADDR.
            const std::string spec = argument.substr(std::strlen("--dump-entry="));
            const size_t first = spec.find(':'), second = spec.find(':', first + 1);
            if (first != std::string::npos && second != std::string::npos) {
                trace_entry_dump(
                    static_cast<uint32_t>(std::strtoul(spec.c_str(), nullptr, 16)),
                    static_cast<uint32_t>(std::strtoul(spec.c_str() + first + 1, nullptr, 16)),
                    static_cast<uint32_t>(std::strtoul(spec.c_str() + second + 1, nullptr, 16)));
            }
            recognised = true;
        }
        if (!recognised && argument.rfind("--", 0) != 0 && options.image_path.empty()) {
            options.image_path = argument;
            recognised = true;
        }
        if (!recognised) {
            usage(argv[0]);
        }
    }
    if (!options.image_path.empty() && options.game_dir.empty()) {
        usage(argv[0]);  // an image without its resources cannot run
    }
    return options;
}

// ---------------------------------------------------------------------------------------------
// Scripted input — the same `FRAME: ACTION` files the emulator's `play --script` reads
// ---------------------------------------------------------------------------------------------

struct ScriptStep {
    unsigned frame;
    std::string action;  // select | menu | play | next | prev | wheel ±N | shot | quit | terminate
};

std::vector<ScriptStep> load_script(const std::string& path) {
    std::vector<ScriptStep> steps;
    if (path.empty()) {
        return steps;
    }
    std::ifstream file(path);
    if (!file) {
        fatal("cannot open script %s", path.c_str());
    }
    std::string line;
    while (std::getline(file, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;  // blank or comment-only
        }
        const unsigned frame = static_cast<unsigned>(std::strtoul(line.c_str(), nullptr, 10));
        std::string action = line.substr(colon + 1);
        action.erase(0, action.find_first_not_of(" \t"));
        action.erase(action.find_last_not_of(" \t\r") + 1);
        steps.push_back({frame, action});
    }
    return steps;
}

// ---------------------------------------------------------------------------------------------
// The click wheel and buttons as the game sees them
// ---------------------------------------------------------------------------------------------

// The word the game tests for button presses: bits 0x01..0x10 are the five buttons, 0x20 is the
// wheel's own "event present". Measured for Mini Golf at 0x18008304 onward (play.rs
// MINIGOLF_FLAGS); the press sets the bit and the next frame clears it, so one frame with the
// bit set is one press.
constexpr uint32_t BUTTON_FLAGS_ADDRESS = 0x1803'7a0cu;

// When the game last saw Menu or Next go down (game/state.h, `InputState`). It learns that from
// the firmware's list of button events, and holding either for four seconds is how an eApp is
// told to suspend itself (game/app.cpp, `handle_long_presses`). This firmware has no event list
// — it sets the flags word above directly — so without these the press time stays at whatever
// start-up left in it, and a press several minutes into a round reads as a hold several minutes
// long: the game asks to be suspended and the screen stops. A press here lasts exactly one
// frame, so "the button went down just now" is the plain truth.
constexpr uint32_t MENU_PRESS_TIME_ADDRESS = 0x1803'7a10u;
constexpr uint32_t NEXT_PRESS_TIME_ADDRESS = 0x1803'7a14u;
constexpr uint32_t CONTEXT_CLOCK = 0x4;  // microseconds at the start of the frame

// The wheel reports one of 120 positions; the game reads a byte derived from it. Both the
// detent count and the transform are the emulator's (play.rs `WHEEL_DETENTS`, `wheel_byte`),
// derived from how the game's input decoder inverts the position.
constexpr int WHEEL_DETENTS = 0x78;

uint8_t wheel_byte(int raw) {
    return static_cast<uint8_t>((((0x77 - raw) * 8) / 3) & 0xff);
}

int wrap_detent(int raw) {
    return ((raw % WHEEL_DETENTS) + WHEEL_DETENTS) % WHEEL_DETENTS;
}

class ClickWheel {
public:
    // A button press: set its flag bit and send a wheel sample so the game dispatches input this
    // frame. The bit is cleared at the start of the next frame by `retire_buttons`.
    void press(Button button) {
        const uint32_t bit = static_cast<uint32_t>(button);
        st32(BUTTON_FLAGS_ADDRESS, ld32(BUTTON_FLAGS_ADDRESS) | bit);
        if (record_press_times_ && context_ != 0 &&
            (button == Button::Menu || button == Button::Next)) {
            const uint32_t now = ld32(context_ + CONTEXT_CLOCK);  // the last frame's clock
            st32(button == Button::Menu ? MENU_PRESS_TIME_ADDRESS : NEXT_PRESS_TIME_ADDRESS, now);
        }
        eapp::queue_input(wheel_byte(raw_));
        held_ |= bit;
    }

    // The context the frame vector is called with, whose clock is what the game compares a press
    // against. Known only once the game is running, so it is handed over rather than found.
    void watch_context(uint32_t context) { context_ = context; }

    // --emulator-firmware reproduces the emulator's own shortcut — the flags word alone, with no
    // press times — so that a run can be compared against a log recorded with it. It is what the
    // oracle uses (tests/diff.sh); nobody should play with it.
    void forget_press_times() { record_press_times_ = false; }

    // Turn the wheel `detents` clicks (negative = the other way). Each detent is queued as its
    // own sample: the game wants to see the position *change* between polls.
    void turn(int detents) {
        const int step = detents < 0 ? -1 : 1;
        for (int i = 0; i != detents; i += step) {
            raw_ = wrap_detent(raw_ + step);
            eapp::queue_input(wheel_byte(raw_));
        }
    }

    // Called first thing each frame: last frame's presses are over.
    void retire_buttons() {
        if (held_ != 0) {
            st32(BUTTON_FLAGS_ADDRESS, ld32(BUTTON_FLAGS_ADDRESS) & ~held_);
            held_ = 0;
        }
    }

    // Called last thing before the frame: a finger resting on the wheel reports continuously,
    // and the game only dispatches input on frames that saw a sample.
    void keep_sample_in_flight() {
        if (eapp::input_queue_empty()) {
            eapp::queue_input(wheel_byte(raw_));
        }
    }

private:
    uint32_t context_ = 0;
    bool record_press_times_ = true;
    int raw_ = 0;
    uint32_t held_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Calling into the game
// ---------------------------------------------------------------------------------------------

// Enter a guest function with up to four arguments, as the firmware would: lr points at a
// return address outside the image, sp is the firmware's stack. Only the registers for the
// arguments given are written; the rest keep whatever the previous call left in them. That
// leftover is observable (the game reads uninitialised registers now and then) and the oracle
// logs depend on it, so the emulator's convention is followed exactly: the frame vector gets
// four arguments, a completion callback two.
void call_guest(Cpu& cpu, uint32_t address, std::initializer_list<uint32_t> arguments) {
    unsigned index = 0;
    for (const uint32_t argument : arguments) {
        cpu.r[index++] = argument;
    }
    cpu.r[LR] = RAM_BASE + RAM_SIZE - 4;
    game::call_indirect(address);
}

// AsyncFileIO request objects: where the game keeps the completion callback and its argument
// (reference/eapp-loader/lib.rs REQ_CALLBACK / REQ_CONTEXT; reversing/asyncfileio-abi.md).
constexpr uint32_t REQUEST_CALLBACK = 0x34;
constexpr uint32_t REQUEST_CONTEXT = 0x38;

// Run the completion callback of every file operation the host finished since last frame. The
// read completion asserts `arg0 == arg1 + 0x128`, so both arguments come from the request.
void deliver_completions(Cpu& cpu) {
    for (const uint32_t request : eapp::take_pending_completions()) {
        const uint32_t callback = ld32(request + REQUEST_CALLBACK);
        const uint32_t context = ld32(request + REQUEST_CONTEXT);
        if (callback != 0) {
            call_guest(cpu, callback, {request, context});
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The frame pump
// ---------------------------------------------------------------------------------------------

// The context RetailOS passes to every vector call, from its eApp task at 0x0024da80: one
// 0x400-byte object, passed as (ctx, ctx + 0x100). `[ctx+0]` is the state byte the firmware
// writes (5 = running) before the first call.
constexpr uint32_t CONTEXT_SIZE = 0x400;
constexpr uint32_t CONTEXT_ANSWER_OFFSET = 0x100;
constexpr uint8_t CONTEXT_STATE_RUNNING = 5;
// What the game writes back once it has agreed to be put away: Exit on the main menu, or Menu
// held down, ask the firmware to suspend the eApp, and the game answers that it has suspended.
constexpr uint8_t CONTEXT_STATE_SUSPENDED = 6;

struct Action {
    bool quit = false;
    bool screenshot = false;
};

Action apply_script_action(const std::string& action, ClickWheel& wheel) {
    Action result;
    // "menu" is the button that opens the pause menu (bit 0x10) and "prev" the 0x02 bit — the
    // same mapping play.rs uses, so existing scripts and recordings keep their meaning.
    if (action == "select") {
        wheel.press(Button::Select);
    } else if (action == "menu") {
        wheel.press(Button::Menu);
    } else if (action == "play") {
        wheel.press(Button::Play);
    } else if (action == "next") {
        wheel.press(Button::Next);
    } else if (action == "prev") {
        wheel.press(Button::Previous);
    } else if (action.rfind("wheel", 0) == 0) {
        wheel.turn(static_cast<int>(std::strtol(action.c_str() + 5, nullptr, 10)));
    } else if (action == "shot") {
        result.screenshot = true;
    } else if (action == "quit" || action == "terminate") {
        result.quit = true;
    } else {
        std::fprintf(stderr, "script: unknown action \"%s\" ignored\n", action.c_str());
    }
    return result;
}

Action apply_platform_input(const platform::FrameInput& input, ClickWheel& wheel) {
    for (const Button button :
         {Button::Select, Button::Menu, Button::Play, Button::Next, Button::Previous}) {
        if (input.buttons & static_cast<uint32_t>(button)) {
            wheel.press(button);
        }
    }
    if (input.wheel_detents != 0) {
        wheel.turn(input.wheel_detents);
    }
    return {input.quit, input.screenshot};
}

// Sound and music the game asked for during the frame, handed to the platform with the paths
// resolved against the game directory (the frameworks work in the game's own relative names).
void forward_audio_requests(platform::Platform& host, const std::string& game_dir) {
    const auto resolved = [&](const std::string& file) {
        return file.empty() || file.front() == '/' ? file : game_dir + "/" + file;
    };
    for (const eapp::SoundRequest& request : eapp::take_sound_requests()) {
        if (request.action == eapp::SoundAction::Stop) {
            host.stop_sound(resolved(request.file));
        } else {
            host.play_sound(resolved(request.file),
                            request.action == eapp::SoundAction::PlayLooping);
        }
    }
    for (const eapp::MusicRequest& request : eapp::take_music_requests()) {
        if (request.action == eapp::MusicAction::Stop) {
            host.stop_music();
        } else {
            host.play_music(resolved(request.file), request.repeat);
        }
    }
    // The device's volume, when the Volume page has moved it. Sent on change rather than every
    // frame so that a platform is free to do real work in it. The two scales are the same
    // number, and this is the one place that knows both.
    static_assert(eapp::AUDIO_LEVEL_MAX == platform::AUDIO_LEVEL_MAX,
                  "the framework's volume scale and the platform's must agree");
    static unsigned last_level = eapp::AUDIO_LEVEL_MAX + 1;  // nothing sent yet
    const unsigned level = eapp::audio_level();
    if (level != last_level) {
        last_level = level;
        host.set_audio_level(level);
    }
}

// The `shot` script action and the P key: write the framebuffer as a PPM next to the build and
// print a hash of it, so two runs can be compared frame for frame without an image viewer.
void save_screenshot(unsigned frame) {
    static unsigned shot_number = 1;
    const uint8_t* rgb = gfx::screen_pixels();
    const unsigned width = gfx::screen_width(), height = gfx::screen_height();
    const size_t size = static_cast<size_t>(width) * height * 3;
    uint32_t hash = 2166136261u;  // FNV-1a
    for (size_t i = 0; i < size; ++i) {
        hash = (hash ^ rgb[i]) * 16777619u;
    }
    // MINIGOLF_SHOT_DIR chooses where they go; the default suits a run from the project root.
    const char* directory = std::getenv("MINIGOLF_SHOT_DIR");
    char path[256];
    std::snprintf(path, sizeof path, "%s/shot-%02u.ppm", directory != nullptr ? directory : "build",
                  shot_number++);
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        std::printf("screenshot frame %u -> cannot write %s (set MINIGOLF_SHOT_DIR)  fnv1a %08x\n",
                    frame, path, hash);
        return;
    }
    std::fprintf(file, "P6\n%u %u\n255\n", width, height);
    std::fwrite(rgb, 1, size, file);
    std::fclose(file);
    std::printf("screenshot frame %u -> %s  fnv1a %08x\n", frame, path, hash);
}

// On a console the only way to see how far start-up got is to say so: there is no terminal behind
// stderr, no debugger, and a fatal message on its own cannot say what had already worked. The
// console shows stdout, and these few lines are worth their space there. A desktop has better
// tools and says nothing.
void boot_stage(const char* what) {
#if defined(__SWITCH__)
    std::printf("  %s\n", what);
#else
    (void)what;
#endif
}

int run(Options options) {
    // The window first: finding the game's files may need to ask the player for them.
    std::unique_ptr<platform::Platform> host =
        platform::create_platform("Mini Golf", options.frames_per_second);
    if (!options.install_zip.empty()) {
        const std::string game_dir =
            (std::filesystem::path(platform::data_directory()) / gamedata::GAME_DIRECTORY_NAME)
                .string();
        std::string why;
        if (!gamedata::install_from_zip(options.install_zip, game_dir, why)) {
            std::fprintf(stderr, "%s: %s\n", options.install_zip.c_str(), why.c_str());
            return EXIT_FAILURE;
        }
        std::printf("installed the game's files to %s\n", game_dir.c_str());
    }
    if (options.game_dir.empty()) {
        options.game_dir = gamedata::locate_game(*host, platform::data_directory());
        if (options.game_dir.empty()) {
            std::fprintf(stderr, "no game files: nothing to run\n");
            return EXIT_FAILURE;
        }
    }
    if (options.image_path.empty()) {
        options.image_path =
            (std::filesystem::path(options.game_dir) / gamedata::GAME_IMAGE_PATH).string();
    }

    if (!options.fixed_time.empty()) {
        int hour = 0, minute = 0;
        if (std::sscanf(options.fixed_time.c_str(), "%d:%d", &hour, &minute) != 2 || hour < 0 ||
            hour > 23 || minute < 0 || minute > 59) {
            std::fprintf(stderr, "--time wants HH:MM, not %s\n", options.fixed_time.c_str());
            return EXIT_FAILURE;
        }
        eapp::set_fixed_host_time(hour, minute);
    }

    boot_stage("game files found");
    guest_memory_init();
    boot_stage("guest memory ready");
    const EAppImage image = load_eapp_image(options.image_path.c_str());
    if (!options.call_log_path.empty()) {
        eapp::call_log().open(options.call_log_path.c_str());
    }
    eapp::set_game_directory(options.game_dir);

    // Where saved games go. The platform may have somewhere of its own; otherwise they sit
    // beside the game, which is where the iPod kept them and where the tests look for them.
    std::unique_ptr<platform::SaveStore> saves = host->create_save_store();
    platform::set_save_store(saves != nullptr
                                 ? std::move(saves)
                                 : platform::make_directory_save_store(options.game_dir));

    // The player's own key bindings, settings and cheats, now that there is somewhere to read
    // them from. Without any, the platform's defaults stand and every cheat is off. --fps= is a
    // deliberate instruction for this run and outranks the saved rate.
    platform::load_input_bindings();
    platform::load_settings();
#if !defined(MINIGOLF_PURE_RECOMPILATION)
    // The port's own files. The pure recompilation is the original code and has none of this in
    // it — src/game/ is not even linked into that build (CMakeLists.txt).
    game::load_cheats();
    game::load_round_history();
    game::set_port_additions_hidden(options.no_port_additions || options.emulator_firmware);
#endif
    if (options.frames_per_second_given) {
        platform::settings().frame_rate = options.frames_per_second;
    }
    if (options.render_scale != 0) {
        platform::settings().render_scale = std::clamp(
            options.render_scale, platform::MIN_RENDER_SCALE, platform::MAX_RENDER_SCALE);
    }
    // The oracles own what the renderer draws: both compare against a 320x240 recording, and a
    // frame of a different size has nothing to compare with. `--emulator-firmware` therefore
    // takes the scale away rather than merely expecting it to be 1.
    if (options.emulator_firmware) {
        platform::settings().render_scale = platform::MIN_RENDER_SCALE;
        eapp::set_emulator_device(true);
    }
    gfx::set_render_threads(options.render_threads);
    host->apply_settings();
    platform::set_text_entry_supported(host->text_input_supported());

    Cpu& cpu = registers();                   // the one register file (runtime/cpu.h)
    cpu.r[SP] = RAM_BASE + RAM_SIZE - 0x100;  // the firmware's stack, near the top of RAM

    // Heap layout compatibility with the emulator: it allocates a 0x90-byte playlist block for a
    // Metadata framework Mini Golf never calls, then the 0x400-byte context, before the game runs.
    // The game's own first allocation must land at the same address, so the same blocks are taken.
    (void)eapp::heap().alloc(0x90);
    const uint32_t context = eapp::heap().alloc(CONTEXT_SIZE);
    st8(context, CONTEXT_STATE_RUNNING);

    boot_stage("image loaded");
    // Init vectors run once, in order; the last one is the per-frame callback.
    for (const uint32_t vector : image.vectors) {
        call_guest(cpu, vector, {context, context + CONTEXT_ANSWER_OFFSET, 0, 0});
    }
    const uint32_t frame_vector = image.vectors.back();

    // A second compatibility allocation: the emulator's 16-byte event node, taken after init.
    (void)eapp::heap().alloc(0x10);

    boot_stage("start-up vectors run");
    const std::vector<ScriptStep> script = load_script(options.script_path);
    size_t next_step = 0;
    ClickWheel wheel;
    wheel.watch_context(context);
    if (options.emulator_firmware) {
        wheel.forget_press_times();
        storage::set_store_stubbed(true);
    }
    // The device's starting volume is the port's choice, and the oracles need the emulator's:
    // it stubbed the level as zero, so both the recordings and the pure recompilation draw the
    // Volume page's bar from there. This is outside the guard below because the pure
    // recompilation links libeapp and has to be told the same thing (game/host_text.h).
    if (options.emulator_firmware || options.no_port_additions) {
        eapp::set_audio_level(0);
    }

    // A quit (from the script or the window) ends the loop at the top of the *next* frame: the
    // frame that saw the quit still runs, as in the emulator, so the two logs end at the same call.
    bool quit_requested = false;
    for (unsigned frame = 0;
         !quit_requested && (options.frame_limit == 0 || frame < options.frame_limit); ++frame) {
        eapp::call_log().begin_frame(frame);
        wheel.retire_buttons();
        // Pushed every frame rather than at start-up: a settings window can change it while the
        // game is running, and the call does nothing when nothing has changed.
        gfx::set_render_scale(platform::settings().render_scale);

        Action action;
        while (next_step < script.size() && script[next_step].frame <= frame) {
            const Action step = apply_script_action(script[next_step].action, wheel);
            action.quit |= step.quit;
            action.screenshot |= step.screenshot;
            ++next_step;
        }
        platform::FrameInput input;
        host->poll(input);
        platform::text_entry_deliver(input.typed);
        const Action live = apply_platform_input(input, wheel);
        quit_requested = action.quit || live.quit;

        deliver_completions(cpu);
        wheel.keep_sample_in_flight();

        call_guest(cpu, frame_vector, {context, context + CONTEXT_ANSWER_OFFSET, 0, 0});

        // Suspended means the game has handed itself back to the firmware. A real iPod would show
        // its own menu again; there is nothing here to go back to, so the program ends — which is
        // what "Exit" on the main menu is asking for. Under --emulator-firmware the loop runs on
        // regardless, because that is what the recordings did.
        if (!options.emulator_firmware && ld8(context) == CONTEXT_STATE_SUSPENDED) {
            quit_requested = true;
        }

        forward_audio_requests(*host, options.game_dir);
        report_frame_dumps(frame);
        if (action.screenshot || live.screenshot) {
            save_screenshot(frame);
        }
        // Not before the game has drawn anything. Its first frame draws nothing — the firmware
        // is telling it that it is running — and the buffer is still the magenta that marks an
        // un-drawn region, which the window would otherwise show as the game's opening screen.
        if (gfx::anything_drawn()) {
    host->present(gfx::screen_pixels(), gfx::screen_width(), gfx::screen_height());
        }
        host->wait_for_next_frame();
    }
    return EXIT_SUCCESS;
}

}  // namespace
}  // namespace minigolf

int main(int argc, char** argv) {
    return minigolf::run(minigolf::parse_options(argc, argv));
}
