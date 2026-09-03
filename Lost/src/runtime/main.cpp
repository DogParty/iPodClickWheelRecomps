// The program: load the game, run its init vectors, then pump frames until told to stop.
//
// This is the recomp's equivalent of the iPod firmware's eApp task, and it copies the emulator's
// frame pump (reference/eapp-loader/play.rs) step for step, because the verification oracle
// compares the two runs call for call. Where the emulator's behaviour was a measured fact about
// the firmware it is cited; where it was the emulator's own convention (scratch allocations,
// the order of input delivery within a frame) it is copied anyway, since the recorded logs
// depend on it.
//
//   lost          [--script=FILE] [--fps=N]
//   lost-headless <image.bin> --gamedir=DIR [--script=FILE] [--call-log=FILE] [--frames=N]
//
// Without --gamedir the game's files are looked for in the platform's data directory
// (platform/paths.h) and, the first time, installed there from the folder or zip the player
// picks (gamedata/install.h). With it — the oracle tests — the directory is used as given and
// nothing is checked or copied.
#include "framework/graphics.h"
#include "framework/music_library.h"
#include "framework/storage.h"
#include "game/cheats.h"
#include "gamedata/install.h"
#include "gamedata/manifest.h"
#include "ipod_eapp.h"
#include "libeapp/heap.h"
#include "platform/input_bindings.h"
#include "platform/paths.h"
#include "platform/platform.h"
#include "platform/settings.h"
#include "platform/text_entry.h"
#include "platform/windows_console.h"
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
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace lost {
namespace {

using platform::Button;

// ---------------------------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------------------------

struct Options {
    std::string image_path;     // defaults to the image inside game_dir
    std::string game_dir;       // the title's directory; empty = find it, or install it
    std::string install_from;   // a folder or zip of the game's files to install before running
    std::string script_path;    // scripted input, FRAME: ACTION per line
    std::string call_log_path;  // where to write the framework-call log
    std::string fixed_time;     // HH:MM shown by the game's clock, for reproducible runs
    std::string program_name;   // argv[0], for a message that says how to run it again
    unsigned frame_limit = 0;   // stop after this many frames; 0 = run until quit
    // Behave as the emulator's own harness did — no button press times, and no quitting when the
    // game suspends itself. Only the oracle wants this; see tests/diff.sh.
    bool emulator_firmware = false;
    // Render the flat-fill pipeline as the emulator does, which is the one rendering decision
    // the two make differently. Only the picture oracle wants this; see tests/frames.sh.
    bool emulator_graphics = false;
    // The pace before the saved settings are read, and what --fps= sets. 30 is the default
    // everywhere (platform/settings.h); the game's own timebase is 60 (miscTBD #9 advances
    // 1/60 s per call), which is what --fps=60 gives.
    unsigned frames_per_second = 30;
    bool frames_per_second_given = false;  // --fps= was asked for, so it beats the saved rate
    // --render-scale= and --cheats=, for a headless run that wants one without a settings file.
    // Both outrank the saved setting for the run they are given on, as --fps= does, and both are
    // refused outright by the two --emulator- flags above.
    unsigned render_scale = 0;          // 0 = whatever the settings say
    unsigned render_threads = 0;        // 0 = one per core; 1 pins it to the calling thread
    bool high_resolution_text = false;  // --hi-res-text
    bool cheats_given = false;          // --cheats= was asked for
    bool unlock_chapters = false;
};

[[noreturn]] void usage(const char* program) {
    std::fprintf(
        stderr,
        "usage: %s [image.bin] [--gamedir=DIR] [--install=FILE-OR-DIR] [--script=FILE] "
        "[--call-log=FILE] [--frames=N] [--fps=N] [--time=HH:MM] "
        "[--trace-entry=ADDR,...] [--dump-entry=ADDR:START:BYTES] [--dump-frame=START:BYTES] "
        "[--render-scale=1..8] [--render-threads=N] [--hi-res-text] "
        "[--cheats=unlock-chapters]\n",
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
    options.program_name = argc > 0 ? argv[0] : "lost";
    const std::pair<const char*, std::string*> text_flags[] = {
        {"--gamedir=", &options.game_dir},       {"--script=", &options.script_path},
        {"--call-log=", &options.call_log_path}, {"--install=", &options.install_from},
        {"--time=", &options.fixed_time},
    };
    const std::pair<const char*, unsigned*> number_flags[] = {
        {"--frames=", &options.frame_limit},
        {"--fps=", &options.frames_per_second},
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
        if (argument == "--emulator-graphics") {
            options.emulator_graphics = true;
            recognised = true;
        }
        if (argument == "--emulator-graphics") {
            options.emulator_graphics = true;
            recognised = true;
        }
        if (argument == "--emulator-firmware") {
            options.emulator_firmware = true;
            recognised = true;
        }
        if (argument == "--hi-res-text") {
            options.high_resolution_text = true;
            recognised = true;
        }
        if (argument.rfind("--render-threads=", 0) == 0) {
            options.render_threads = static_cast<unsigned>(
                std::strtoul(argument.c_str() + std::strlen("--render-threads="), nullptr, 10));
            recognised = true;
        }
        if (argument.rfind("--render-scale=", 0) == 0) {
            options.render_scale = static_cast<unsigned>(
                std::strtoul(argument.c_str() + std::strlen("--render-scale="), nullptr, 10));
            recognised = true;
        }
        if (argument.rfind("--cheats=", 0) == 0) {
            // A comma-separated list of names, so a second cheat needs no second flag. An
            // unknown name is a mistake worth stopping for rather than a run that quietly does
            // not cheat.
            std::istringstream names(argument.substr(std::strlen("--cheats=")));
            std::string name;
            options.cheats_given = true;
            while (std::getline(names, name, ',')) {
                if (name == "unlock-chapters") {
                    options.unlock_chapters = true;
                } else if (!name.empty() && name != "none") {
                    std::fprintf(stderr, "no such cheat: %s\n", name.c_str());
                    usage(argv[0]);
                }
            }
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

// How a button press reaches this game.
//
// It has no button flags word — the address other titles poll does not exist here — and no
// press-time words either, so a held Menu cannot be told from a tap. Presses arrive the way the
// firmware really delivered them: as a node on an event list whose head the eApp task publishes
// at `ctx+0x30` before each frame call. Apple's `postEvent` at 0x0024d918 builds the node and
// appends it; the game hands the head straight to its own dispatcher.
//
// The node is twelve bytes: a type, a state, a payload, and the next node. The state byte is
// what says press or release, and the game's dispatcher at 0x18007fc8 reads exactly two values
// and maps anything else to nothing:
//
//     ldrb r0,[r4,#1] / cmp r0,#1 -> 1 / cmp r0,#2 -> 2 / else 0
//
// (reference/eapp-loader/play.rs, `post_event`, which arrived at all of this by finding that the
// wheel moved the name-entry highlight while Select never picked a letter.)
constexpr uint32_t CONTEXT_EVENT_LIST = 0x30;
constexpr uint32_t EVENT_NODE_SIZE = 0x10;
constexpr uint32_t EVENT_NODE_STATE = 0x01;
constexpr uint32_t EVENT_NODE_PAYLOAD = 0x04;
constexpr uint32_t EVENT_NODE_NEXT = 0x08;
//
// Those two values are a press and *the button still being down*, not a press and a release.
// The game's own handler at 0x18005f38 acts on state 1 and on nothing else — every button it
// knows tests `cmp r7,#1` before doing anything — while state 2 is read by the dispatcher above
// as "Menu is being held" and starts the four-second countdown that puts an eApp away:
//
//     0x18008010  cmp r7,#2 / movne r0,#0 / moveq r0,#1 / strb r0,[r8,#2]   ; menu_held = held
//     0x1803d6b8  ldrb r0,[r9,#2] ... subne r0,r6,r0 / cmpne r0,#0x3d0900   ; held over 4 s?
//                 movgt r0,#3 / strbgt sl,[r4]                              ; answer 5: suspend
//
// So sending state 2 to end a press told the game the button had just gone *down* and stayed
// there. Nothing ever arrived to clear it, and four seconds after the first Menu tap the game
// began asking to be suspended and never stopped — which is the request the frame loop below
// now honours, and would have quit the game a second after the player opened the pause menu.
// A release is neither of the two values: the dispatcher maps anything else to 0, which clears
// the held flag and asks for nothing. State 2 is not sent at all — the platform layer reports
// button *edges*, not whether one is still down (platform/platform.h, `FrameInput::buttons`), so
// there is no held state here to report. Holding Menu to put the game away is what that would
// buy, and it is not worth inventing a hold the host never told us about.
constexpr uint8_t EVENT_STATE_PRESSED = 1;
constexpr uint8_t EVENT_STATE_RELEASED = 3;

// A posted node is on the list for the frame that posted it and comes off at the start of the
// next one. Leaving it there longer is not harmless: a node nothing retires is re-read as a
// fresh press every frame, which is what once made other titles behave as though Select were
// being clicked over and over on their own. Leaving it one frame too long is enough to move
// every button dispatch a frame late, which is how this was found — the wheel's gestures landed
// on the frame the recording said and the button's did not.

// The event type each button is delivered as.
//
// Menu and Previous are not where their names would put them, and that is deliberate: these are
// the types the emulator posts for each of its own buttons (reference/eapp-loader/play.rs,
// `event_type_for` together with the script's own mapping), and the recordings this project is
// compared against were made with them. What the game *calls* type 1 and type 3 is not
// established here — only which key produces which — so a swap would be a guess, not a fix.
uint8_t event_type_for(Button button) {
    switch (button) {
    case Button::Menu:
        return 1;
    case Button::Select:
        return 2;
    case Button::Previous:
        return 3;
    case Button::Play:
        return 4;
    case Button::Next:
        return 5;
    }
    return 0;
}

// The wheel reports one of 120 positions; the game reads a byte derived from it. Both the
// detent count and the transform are the emulator's (play.rs `WHEEL_DETENTS`, `wheel_byte`),
// derived from how the game's input decoder inverts the position.
constexpr int WHEEL_DETENTS = 0x78;

// How long a scroll key must be held before it stops being a flick and becomes a turn that keeps
// going. Twelve frames is a fifth of a second — longer than anyone taps, and longer than the
// eight detents a press queues take to drain, which is what keeps a tap worth exactly one row.
constexpr unsigned SPIN_DELAY_FRAMES = 12;

// A spin gathers pace, as a thumb going round a wheel does. One detent a frame is half a turn a
// second, which is a crawl and not a spin; six is three turns a second, which is about as fast
// as a hand goes. Starting slow is what keeps a menu readable for the first moment of a hold,
// and the game reads the *speed* as well as the direction — its own tutorial asks for the wheel
// to be spun, and a meter that fills at a crawl is not the same input as one spun in earnest.
constexpr int SPIN_DETENTS_MAX = 6;
constexpr unsigned SPIN_RAMP_FRAMES = 12;  // frames of holding per extra detent a frame

uint8_t wheel_byte(int raw) {
    return static_cast<uint8_t>((((0x77 - raw) * 8) / 3) & 0xff);
}

// Where the four sides of the wheel sit, as position bytes.
//
// Byte 0 is at three o'clock and the angle runs *counter-clockwise*, so the cardinal points are
// right 0, top 64, left 128, bottom 192 — not the clockwise-from-top order that looks natural.
// Measured against this game's own movement: an even compass-order assignment came out with top
// and right transposed and left and bottom transposed, which is exactly this rotation
// (reference/eapp-loader/play.rs).
//
// A byte, and not a detent, on purpose. `wheel_byte` maps 120 detents onto 320 byte units — a
// turn and a quarter — so quartering the detent space gives four positions 176, -80 and -80
// apart, which is not four cardinal points. The position byte *is* the angle.
uint8_t touch_byte(platform::WheelTouch side) {
    switch (side) {
    case platform::WheelTouch::Right:
        return 0;
    case platform::WheelTouch::Top:
        return 64;
    case platform::WheelTouch::Left:
        return 128;
    case platform::WheelTouch::Bottom:
        return 192;
    case platform::WheelTouch::None:
        break;
    }
    return 0;
}

// A tap of the wheel's left or right side is a flick of it, and moves a menu by one.
//
// How long a press may last and still be a tap rather than a finger resting there to walk, and
// how far the flick that follows it moves the position. The distance has to clear the game's own
// notch threshold, which it reads per screen from `[app+0x1104c]` and which was measured at 13,
// 20, 30 and 40 units on the screens this game has been walked through — so 42 clears all of
// them, and a screen that ever wanted more would simply not step, which is the harmless way to
// be wrong.
//
// Two samples, not one per detent as `turn` does. The game takes one sample a frame, so the
// sixteen detents this distance is worth would be sixteen frames of the wheel being touched at a
// moving place — half a second of the character wandering, if the tap happened during play. Two
// is enough for the game to see the position move, and is over before it can be walked on.
constexpr unsigned TAP_FRAMES = 10;
constexpr int TAP_FLICK_UNITS = 42;
constexpr int TAP_FLICK_SAMPLES = 2;

// How far one tap of a wheel side moves the position, and which way.
//
// A menu moves on the wheel's *delta*, never on where the finger is: the game's decoder at
// 0x18008074 takes the position byte, subtracts the previous one, sign-extends the difference to
// eight bits and hands the sign to `f_18005f38(6, ...)`, which turns it into UI event 0x11 or
// 0x12 — one row back, or one row on. A finger resting on a side reports the same byte every
// frame, so its delta is zero and no menu ever moves, which is why holding left or right walked
// the character but did nothing on the pause menu.
//
// One unit is enough, because only the sign is read. The direction is inverted because the
// position byte is: `wheel_byte` runs it counter-clockwise (`(0x77 - raw) * 8 / 3`), so the
// *falling* byte is the forward turn that moves a menu on.
//
// Only the two sides that read as back and forward get one. Up and bottom stay silent: this
// game's menus are horizontal rows, and a tap upward that moved the selection sideways would be
// worse than one that does nothing.
int step_for_tap(platform::WheelTouch side) {
    switch (side) {
    case platform::WheelTouch::Left:
        return 1;
    case platform::WheelTouch::Right:
        return -1;
    case platform::WheelTouch::Top:
    case platform::WheelTouch::Bottom:
    case platform::WheelTouch::None:
        break;
    }
    return 0;
}

int wrap_detent(int raw) {
    return ((raw % WHEEL_DETENTS) + WHEEL_DETENTS) % WHEEL_DETENTS;
}

class ClickWheel {
public:
    // Where the event list lives and where its one node is built. Both are known only once the
    // game is running, so they are handed over rather than found.
    void use_event_list(uint32_t context, uint32_t node) {
        context_ = context;
        node_ = node;
    }

    // A button went down. The release follows at the start of the next frame.
    void press(Button button) {
        post_event(event_type_for(button), EVENT_STATE_PRESSED);
        held_ = button;
        holding_ = true;
    }

    // Turn the wheel `detents` clicks (negative = the other way). When the queue runs dry the
    // poll answers zero, which is the wheel reporting that nothing is touching it — the release
    // that ends a gesture, and the reason nothing refills it between frames.
    // Each detent is queued as its
    // own sample: the game wants to see the position *change* between polls.
    void turn(int detents) {
        const int step = detents < 0 ? -1 : 1;
        for (int i = 0; i != detents; i += step) {
            raw_ = wrap_detent(raw_ + step);
            eapp::queue_input(wheel_byte(raw_));
        }
    }

    // The wheel being turned and not let go of: one detent a frame for as long as it lasts.
    //
    // This is the gesture a flick cannot stand in for. `turn` queues its detents and then lets
    // the queue run dry, which the game reads as the finger lifting — so eight of them in a row
    // are eight separate flicks, and anything that asks for a *sustained* turn never sees one.
    // Here the contact never breaks: every frame moves the position by one and asserts it.
    void spin(int direction) {
        if (direction == 0) {
            if (spin_frames_held_ > SPIN_DELAY_FRAMES) {
                eapp::clear_input_queue();  // the finger comes off; the queue runs dry
            }
            spin_frames_held_ = 0;
            return;
        }
        // A tap of a scroll key is a flick worth one row and nothing else — `turn` has already
        // queued its eight detents. Only a key held past a tap becomes a turn that keeps going,
        // and the delay is longer than those eight detents take to drain, so the row a press is
        // worth always arrives whole before the spin takes the queue over.
        if (++spin_frames_held_ <= SPIN_DELAY_FRAMES) {
            return;
        }
        const unsigned spinning = spin_frames_held_ - SPIN_DELAY_FRAMES;
        const int detents =
            std::min<int>(SPIN_DETENTS_MAX, 1 + static_cast<int>(spinning / SPIN_RAMP_FRAMES));
        raw_ = wrap_detent(raw_ + (direction > 0 ? detents : -detents));
        // One sample, not one per detent: a finger going round the wheel reports where it is now,
        // and the game reads the distance it has moved since the last poll as the speed.
        eapp::clear_input_queue();
        eapp::queue_input(wheel_byte(raw_));
    }

    // A finger resting on one side of the wheel, or lifted. Called once a frame with whatever is
    // being held now: a touch is a position, not an event, and the game walks for as long as it
    // is asserted.
    //
    // The queue is emptied first. A held finger reports the *same* place every frame, and any
    // samples still queued behind it are from somewhere else — left there, the game reads the
    // difference as the wheel turning. Letting the queue run dry on release is what tells it the
    // finger has gone (src/libeapp/input.cpp).
    void touch(platform::WheelTouch side) {
        if (side == platform::WheelTouch::None) {
            if (touching_) {
                eapp::clear_input_queue();
                touching_ = false;
                flick_after_tap();
            }
            tapped_side_ = platform::WheelTouch::None;
            touch_frames_ = 0;
            return;
        }
        if (side != tapped_side_) {
            tapped_side_ = side;
            touch_frames_ = 0;
        }
        ++touch_frames_;
        eapp::clear_input_queue();
        eapp::queue_input(touch_byte(side));
        touching_ = true;
    }

    // Whether a tap of the wheel's left or right side flicks it. Off for the oracles, whose
    // recordings were made without it (--emulator-firmware).
    void set_steps_on_tap(bool steps) { steps_on_tap_ = steps; }

    // The finger has just come off. If it was only there for a moment, and on a side that reads
    // as back or forward, it was a flick rather than the beginning of a walk: send the turn now,
    // when there is no walk left for a moving position to spoil.
    void flick_after_tap() {
        const int direction = step_for_tap(tapped_side_);
        if (!steps_on_tap_ || touch_frames_ > TAP_FRAMES || direction == 0) {
            return;
        }
        const int base = touch_byte(tapped_side_);
        for (int i = 1; i <= TAP_FLICK_SAMPLES; ++i) {
            eapp::queue_input(
                static_cast<uint8_t>(base + direction * TAP_FLICK_UNITS * i / TAP_FLICK_SAMPLES));
        }
    }

    // `hold <side> <frames>` from a script: the same finger, held for a fixed count, so a case
    // can walk the character without a keyboard. Answers the side to assert this frame.
    void hold_for(platform::WheelTouch side, unsigned frames) {
        held_side_ = side;
        held_frames_ = frames;
    }

    // `spin <direction> <frames>` from a script: the same sustained turn, for a fixed count.
    void spin_for(int direction, unsigned frames) {
        spin_direction_ = direction;
        spin_frames_ = frames;
    }

    [[nodiscard]] int scripted_spin() {
        if (spin_frames_ == 0) {
            return 0;
        }
        --spin_frames_;
        return spin_direction_;
    }

    [[nodiscard]] platform::WheelTouch scripted_touch() {
        if (held_frames_ == 0) {
            return platform::WheelTouch::None;
        }
        --held_frames_;
        return held_side_;
    }

    // Called first thing each frame: a button held since last frame is now released, and a node
    // that has had its frame comes off the list.
    void retire_buttons() {
        if (holding_) {
            post_event(event_type_for(held_), EVENT_STATE_RELEASED);
            holding_ = false;
            return;  // the release node takes the list for this frame
        }
        if (node_posted_ && context_ != 0) {
            st32(context_ + CONTEXT_EVENT_LIST, 0);  // an empty list is a null head
            node_posted_ = false;
        }
    }

private:
    // Build the node, publish it as the whole list, and send a wheel sample alongside it.
    //
    // The sample is not decoration. The game only looks at the event list on a frame whose input
    // flags are non-zero (`0x18018a44: ldr r0,[r9,#0x14] / cmp r0,#0 / beq`), and those are set
    // only by a poll that reported an event — so a button pressed while the wheel is still is
    // never read at all.
    void post_event(uint8_t type, uint8_t state) {
        if (context_ == 0 || node_ == 0) {
            return;
        }
        st8(node_, type);
        st8(node_ + EVENT_NODE_STATE, state);
        st32(node_ + EVENT_NODE_PAYLOAD, 0);
        st32(node_ + EVENT_NODE_NEXT, 0);  // a list of one
        st32(context_ + CONTEXT_EVENT_LIST, node_);
        eapp::queue_input(wheel_byte(raw_));
        node_posted_ = true;
    }

    uint32_t context_ = 0;
    uint32_t node_ = 0;
    int raw_ = 0;
    Button held_ = Button::Select;
    bool holding_ = false;
    bool node_posted_ = false;
    bool touching_ = false;
    unsigned spin_frames_held_ = 0;
    // The flick a tap becomes: which side the finger is on, and how long it has been there. See
    // step_for_tap and flick_after_tap.
    platform::WheelTouch tapped_side_ = platform::WheelTouch::None;
    unsigned touch_frames_ = 0;
    bool steps_on_tap_ = true;
    platform::WheelTouch held_side_ = platform::WheelTouch::None;
    unsigned held_frames_ = 0;
    int spin_direction_ = 0;
    unsigned spin_frames_ = 0;
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
// 0x400-byte object, passed as (ctx, ctx + 0x100). `[ctx+0]` is the byte the firmware writes
// before every call to say *why* it is calling.
constexpr uint32_t CONTEXT_SIZE = 0x400;
constexpr uint32_t CONTEXT_ANSWER_OFFSET = 0x100;

// The reasons this game understands. Its frame loop at 0x1803d6ac reads the byte and branches
// three ways, which is where these values come from:
//
//     ldrb r0,[r5,#0] / cmp r0,#1 / bne 0x1803d7a0     ; 1 = run a normal frame
//     0x1803d7a0:      cmp r0,#5 / bne 0x1803d864      ; 5 = (re)initialise
//     0x1803d864:      mov r0,#1 / bl 0x180062a4       ; anything else = shut down
//
// So the byte is not a state the firmware sets once. Left at 5 the game re-runs its init path
// every frame and tears the level down again as it goes — the emulator measured 336 release-all
// calls in 9 000 frames, and four times less drawn (reference/eapp-loader/play.rs,
// `defaults_for`, which gives this title `--frame-reason=first0:1`).
constexpr uint8_t CONTEXT_REASON_FIRST_FRAME = 0;  // the frame that follows initialisation
constexpr uint8_t CONTEXT_REASON_FRAME = 1;        // every frame after it
constexpr uint8_t CONTEXT_REASON_INITIALISE = 5;   // what the init vectors are called with

// The byte the game answers in, at `ctx+0x100` — the second argument every vector is given.
//
// It is not a status the firmware may ignore. It is the reason the game is asking to be called
// with *next*, and the last thing the frame vector does is copy it into the reason byte itself:
//
//     0x1803d82c  ldrb r0,[r4] / strb r0,[r5]      ; r4 = ctx+0x100, r5 = ctx
//
// so a firmware that leaves `[ctx+0]` alone honours the request by doing nothing at all. This
// loop wrote its own reason over the top of it every frame, and the two requests the game can
// make were therefore both discarded:
//
//   * 5 — SUSPEND. "I am finished; call me back to shut down." The game's own tick returning 0
//     is what raises it (0x1803d790: `bl 0x180063b8 / cmp fp,#0 / strbeq sl,[r4]`), and Save
//     and Exit is exactly that: the save is written, the tick answers 0, and the game sits on
//     its SAVING screen waiting for a call that never came. That is the hang this fixes; the
//     pinned emulator hangs there too, having the same fixed `--frame-reason=first0:1`.
//   * 6 — SUSPENDED. Answered from the reason-5 path once the teardown is done. The eApp is
//     finished and the firmware would take its task away. There is no iPod menu to go back to
//     here, so the program ends.
constexpr uint32_t CONTEXT_ANSWER_STATE = 0x00;
constexpr uint8_t CONTEXT_STATE_SUSPEND = 5;
constexpr uint8_t CONTEXT_STATE_SUSPENDED = 6;

// *Why* the game is asking. It records that alongside every one of those requests, in the byte
// at `[r9+7]` where r9 is its input-state block — the literal at 0x1803d874 — and there is one
// value per site that raises the request:
//
//     0x1803d6d0  movgt r0,#3 / strbgt sl,[r4] / strbgt r0,[r9,#7]   ; Menu held over 4 s
//     0x1803d6f4  movgt r0,#4 / strbgt sl,[r4] / strbgt r0,[r9,#7]   ; Next held over 2 s
//     0x1803d738  movlt r0,#2 / strblt sl,[r4] / strblt r0,[r9,#7]   ; idle past the sleep timer
//     0x1803d798  cmp fp,#0   / strbeq sl,[r4] / strbeq r7,[r9,#7]   ; its own tick returned 0
//
// All four were watched happening: 3 four seconds after a Menu tap, 2 after twenty seconds of
// nobody pressing anything, and 1 on the frame after Save and Exit wrote the save.
//
// Only the idle one is refused here. The iPod dimmed and then slept when it was left alone, and
// the game asks to be put away when that happens; a window on a desktop has no such policy, and
// the player watching a cutscene has not gone anywhere. Refusing it costs nothing — the game
// asks again every frame, harmlessly, until the next input refreshes its activity clock — and
// honouring it would end the game a few seconds after the player stopped touching the keyboard.
constexpr uint32_t SUSPEND_REASON_ADDRESS = 0x1804067bu;
constexpr uint8_t SUSPEND_BECAUSE_IDLE = 2;

struct Action {
    bool quit = false;
    bool screenshot = false;
};

// How long `hold` lasts when the script does not say. The emulator's own default, so that a
// script written for one runs the same on the other.
constexpr unsigned DEFAULT_HOLD_FRAMES = 60;

platform::WheelTouch side_from_name(const std::string& name) {
    if (name == "right") {
        return platform::WheelTouch::Right;
    }
    if (name == "bottom") {
        return platform::WheelTouch::Bottom;
    }
    if (name == "left") {
        return platform::WheelTouch::Left;
    }
    return platform::WheelTouch::Top;
}

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
    } else if (action.rfind("hold ", 0) == 0) {
        // `hold <side> [frames]` — pin a finger to one side of the wheel, which is how a script
        // walks the character. The same spelling the emulator's own scripts use, so a case runs
        // on both and the picture oracle can compare them (tests/frames.sh).
        std::string side_name;
        unsigned frames = DEFAULT_HOLD_FRAMES;
        std::istringstream fields(action.substr(std::strlen("hold ")));
        fields >> side_name >> frames;
        wheel.hold_for(side_from_name(side_name), frames);
    } else if (action.rfind("spin ", 0) == 0) {
        // `spin <+|-> [frames]` — turn the wheel and keep turning it, which is what a held scroll
        // key does. Not the same as `wheel N`, and the difference is the whole point: that one
        // lets go afterwards.
        std::string direction;
        unsigned frames = DEFAULT_HOLD_FRAMES;
        std::istringstream fields(action.substr(std::strlen("spin ")));
        fields >> direction >> frames;
        wheel.spin_for(direction == "-" ? -1 : 1, frames);
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
            host.stop_sound(request.voice);
            continue;
        }
        const platform::SoundClip clip{request.samples.data(), request.samples.size(),
                                       request.sample_rate, request.channels, request.bits};
        host.play_sound(request.voice, clip, request.action == eapp::SoundAction::PlayLooping);
    }
    for (const eapp::MusicRequest& request : eapp::take_music_requests()) {
        host.play_music(resolved(request.file), request.repeat);
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
    // LOST_SHOT_DIR chooses where they go; the default suits a run from the project root.
    const char* directory = std::getenv("LOST_SHOT_DIR");
    char path[256];
    std::snprintf(path, sizeof path, "%s/shot-%02u.ppm", directory != nullptr ? directory : "build",
                  shot_number++);
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        std::printf("screenshot frame %u -> cannot write %s (set LOST_SHOT_DIR)  fnv1a %08x\n",
                    frame, path, hash);
        return;
    }
    std::fprintf(file, "P6\n%u %u\n255\n", width, height);
    std::fwrite(rgb, 1, size, file);
    std::fclose(file);
    std::printf("screenshot frame %u -> %s  fnv1a %08x\n", frame, path, hash);
}

int run(Options options) {
    // The window first: finding the game's files may need to ask the player for them.
    std::unique_ptr<platform::Platform> host =
        platform::create_platform("Lost", options.frames_per_second);
    // --install=PATH installs from a folder or a zip without opening the browser, which is what
    // a headless run needs and what a second machine's setup script wants.
    if (!options.install_from.empty()) {
        const std::string game_dir =
            (std::filesystem::path(platform::data_directory()) / gamedata::GAME_DIRECTORY_NAME)
                .string();
        std::string why;
        const bool from_directory = std::filesystem::is_directory(options.install_from);
        const bool installed =
            from_directory ? gamedata::install_from_directory(options.install_from, game_dir, why)
                           : gamedata::install_from_zip(options.install_from, game_dir, why);
        if (!installed) {
            std::fprintf(stderr, "%s: %s\n", options.install_from.c_str(), why.c_str());
            return EXIT_FAILURE;
        }
        std::printf("installed the game's files to %s\n", game_dir.c_str());
    }
    // --gamedir runs straight from a directory and checks nothing, which is what the tests want:
    // each of them is handed its own copy. Without it the game lives in the player's data
    // directory, and the first launch asks them where to install it from.
    if (options.game_dir.empty()) {
        options.game_dir = gamedata::locate_game(*host, platform::data_directory());
        if (options.game_dir.empty()) {
            std::fprintf(stderr,
                         "no game files: nothing to run. To install them without the file "
                         "browser: %s --install-zip=PATH-TO-THE-GAME.zip\n",
                         options.program_name.c_str());
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

    guest_memory_init();
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

    // The player's own key bindings and settings, now that there is somewhere to read them from.
    // Without any, the platform's defaults stand. --fps= is a deliberate instruction for this run
    // and outranks the saved rate.
    platform::load_input_bindings();
    platform::load_settings();
    if (options.frames_per_second_given) {
        platform::settings().frame_rate = options.frames_per_second;
    }
    if (options.render_scale != 0) {
        platform::settings().render_scale = std::clamp(
            options.render_scale, platform::MIN_RENDER_SCALE, platform::MAX_RENDER_SCALE);
    }
    if (options.high_resolution_text) {
        platform::settings().high_resolution_text = true;
    }
    if (options.cheats_given) {
        platform::settings().unlock_all_chapters = options.unlock_chapters;
    }
    // The oracles own what the game does, as they own what the renderer draws: these settings
    // come out of a file the player owns, and no recorded case may depend on what is in it. A
    // cheat is refused outright under either flag rather than merely being expected to be off.
    if (options.emulator_firmware || options.emulator_graphics) {
        platform::settings().unlock_all_chapters = false;
    }
    host->apply_settings();
    platform::set_text_entry_supported(host->text_input_supported());

    Cpu& cpu = registers();                   // the one register file (runtime/cpu.h)
    cpu.r[SP] = RAM_BASE + RAM_SIZE - 0x100;  // the firmware's stack, near the top of RAM

    // Two blocks come out of the game's heap before the game itself allocates anything: the
    // music library's now-playing playlist (framework/music_library.h) and this context. Their
    // order and size decide where the game's own first allocation lands, and the oracle compares
    // those addresses, so both are taken here and in this order.
    music::reserve_playlist();
    const uint32_t context = eapp::heap().alloc(CONTEXT_SIZE);
    st8(context, CONTEXT_REASON_INITIALISE);

    // Init vectors run once, in order; the last one is the per-frame callback. Slot 1 is the
    // terminate entry and is *not* an initialiser — running it here would take the game down
    // before it had drawn a frame. It is still the frame vector if it happens to be the last
    // one; what is skipped is calling it now.
    for (const EAppVector& vector : image.vectors) {
        if (vector.slot == TERMINATE_VECTOR_SLOT) {
            continue;
        }
        call_guest(cpu, vector.address, {context, context + CONTEXT_ANSWER_OFFSET, 0, 0});
    }
    const uint32_t frame_vector = image.vectors.back().address;

    // The one event node, taken from the game's heap after initialisation — the same place and
    // the same moment the emulator takes it, so the addresses either side of it agree.
    const uint32_t event_node = eapp::heap().alloc(EVENT_NODE_SIZE);

    const std::vector<ScriptStep> script = load_script(options.script_path);
    size_t next_step = 0;
    ClickWheel wheel;
    wheel.use_event_list(context, event_node);
    if (options.emulator_firmware) {
        storage::set_store_stubbed(true);
        eapp::set_emulator_device(true);
    }
    wheel.set_steps_on_tap(!options.emulator_firmware);
    // This game re-points every attribute it wants immediately before every draw — the four
    // textured paths point attribute 0 then 1, the flat path points 0 alone — so "pointed since
    // the last draw" is what says which attributes a draw reads. See gfx::set_attributes_
    // repointed_per_draw; the other titles sharing this rasteriser do not do it and must not
    // claim it.
    gfx::set_attributes_repointed_per_draw(true);
    gfx::set_emulator_graphics(options.emulator_graphics);
    // How many threads draw a frame. Not a setting about the picture — every frame is
    // bit-for-bit what one thread would have drawn (framework/graphics.h) — so the oracles
    // run with it too, and it is here only so that something can be measured against one.
    gfx::set_render_threads(options.render_threads);
    if (game::any_cheat_enabled()) {
        // Said once, at the top of the run. A cheat changes what the game does, and a log or a
        // screenshot from a run with one on should not look like a log from a run without.
        std::printf("cheats: unlock all chapters\n");
    }

    // A quit (from the script or the window) ends the loop at the top of the *next* frame: the
    // frame that saw the quit still runs, as in the emulator, so the two logs end at the same call.
    bool quit_requested = false;
    // The reason the *game* asked for, carried to the next call. See CONTEXT_STATE_* above.
    uint8_t next_reason = CONTEXT_REASON_FRAME;
    for (unsigned frame = 0;
         !quit_requested && (options.frame_limit == 0 || frame < options.frame_limit); ++frame) {
        eapp::call_log().begin_frame(frame);
        wheel.retire_buttons();
        // The renderer's two options, pushed every frame rather than at start-up: the settings
        // window can change either of them while the game is running, and both calls do nothing
        // when nothing has changed. `--emulator-graphics` is the exception and owns the renderer
        // outright, because the picture oracle has nothing to compare a different-sized frame
        // with (framework/graphics.h).
        if (!options.emulator_graphics) {
            gfx::set_render_scale(platform::settings().render_scale);
            gfx::set_high_resolution_text(platform::settings().high_resolution_text);
        }

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

        // The finger on the wheel, asserted last so it has the final say on what is queued. A
        // script's `hold` outranks the keyboard, which is the order the emulator applies them in
        // and therefore the order a recording was made in.
        //
        // Resting on the wheel and turning it are one finger doing one of two things, so at most
        // one of them speaks each frame. A rest wins: it is the more deliberate gesture, and a
        // player holding a direction to walk is not also asking the wheel to turn.
        const platform::WheelTouch scripted = wheel.scripted_touch();
        const platform::WheelTouch resting =
            scripted != platform::WheelTouch::None ? scripted : input.touch;
        const int spin = wheel.scripted_spin();
        if (resting != platform::WheelTouch::None) {
            wheel.spin(0);
            wheel.touch(resting);
        } else {
            wheel.touch(platform::WheelTouch::None);
            wheel.spin(spin != 0 ? spin : input.spin);
        }

        // Why the firmware is calling, this frame. Zero on the first frame only: the game has
        // just been initialised and reads that as "you are now running", and there is no way to
        // ask it whether it has noticed — the byte it would answer in is the byte being written.
        // The count of frames is the only signal left. See CONTEXT_REASON_* above.
        st8(context, frame == 0 ? CONTEXT_REASON_FIRST_FRAME : next_reason);
        call_guest(cpu, frame_vector, {context, context + CONTEXT_ANSWER_OFFSET, 0, 0});

        // What the game asked to be called with next. Only the two states it is known to answer
        // are honoured — a byte this loop does not recognise is treated as an ordinary frame,
        // because the game's own dispatcher reads *anything* other than 1 or 5 as "shut down"
        // and a misread would take the game away mid-play. Under --emulator-firmware the
        // request is ignored entirely and every frame is reason 1, because that is the fixed
        // reason the recordings were made with and the logs are compared call for call.
        const auto answered =
            static_cast<uint8_t>(ld8(context + CONTEXT_ANSWER_OFFSET + CONTEXT_ANSWER_STATE));
        next_reason = CONTEXT_REASON_FRAME;
        if (!options.emulator_firmware) {
            if (answered == CONTEXT_STATE_SUSPEND &&
                ld8(SUSPEND_REASON_ADDRESS) != SUSPEND_BECAUSE_IDLE) {
                next_reason = CONTEXT_STATE_SUSPEND;
            } else if (answered == CONTEXT_STATE_SUSPENDED) {
                quit_requested = true;
            }
        }

        forward_audio_requests(*host, options.game_dir);
        report_frame_dumps(frame);
        if (action.screenshot || live.screenshot) {
            save_screenshot(frame);
        }
        // The cheats a player has asked for, after the game's own frame and before the picture
        // that frame produced is shown — a cheat that undoes something the game does while it
        // runs has to run after the run that does it. See src/game/cheats.h.
        game::apply_cheats();
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
}  // namespace lost

int main(int argc, char** argv) {
    // On Windows this is a windowed program with no console of its own: it joins the terminal
    // that started it, if there was one, and otherwise says anything fatal in a message box.
    // Everywhere else, and in the headless build, it does nothing at all.
    lost::platform::windows_console_begin("Lost");
    return lost::run(lost::parse_options(argc, argv));
}
