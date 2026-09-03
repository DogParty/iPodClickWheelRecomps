// The program: load the game, run its init vectors, then pump frames until told to stop.
//
// This is the recomp's equivalent of the iPod firmware's eApp task, and it copies the emulator's
// frame pump (reference/eapp-loader/play.rs) step for step, because the verification oracle
// compares the two runs call for call. Where the emulator's behaviour was a measured fact about
// the firmware it is cited; where it was the emulator's own convention (scratch allocations,
// the order of input delivery within a frame) it is copied anyway, since the recorded logs
// depend on it.
//
//   bowling          [--script=FILE] [--fps=N]
//   bowling-headless <image.bin> --gamedir=DIR [--script=FILE] [--call-log=FILE] [--frames=N]
//
// Without --gamedir the game's files are looked for in the platform's data directory
// (platform/paths.h) and, the first time, installed there from the folder or zip the player
// picks (gamedata/install.h). With it — the oracle tests — the directory is used as given and
// nothing is checked or copied.
//
// Ported from the Texas Hold'em recomp's main.cpp (reference/PORTED.md). Two parts are this
// title's own and are marked below: how a button press reaches the game (an event list, the
// model the Lost recomp's main.cpp carries, with the emulator's own node timing) and what the
// reason byte does (a handshake — PLAN.md differences 1 and 2).
#include "framework/graphics.h"
#include "framework/music_library.h"
#include "framework/storage.h"
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

namespace bowling {
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
    unsigned trace_from = 0;    // --trace-from=N: the entry trace is silent before frame N
    // Behave as the emulator's own harness did — refuse the game's file writes, answer the store
    // "not ready". Only the oracle wants this; see tests/diff.sh.
    bool emulator_firmware = false;
    // Render the flat-fill pipeline as the emulator does, which is the one rendering decision
    // the two make differently. Only the picture oracle wants this; see tests/frames.sh.
    bool emulator_graphics = false;
    // The pace before the saved settings are read, and what --fps= sets. 30 is the default
    // everywhere (platform/settings.h); the game's own timebase is 60 (miscTBD #9 advances
    // 1/60 s per call), which is what --fps=60 gives.
    unsigned frames_per_second = 30;
    bool frames_per_second_given = false;  // --fps= was asked for, so it beats the saved rate
    // --render-scale=, for a headless run that wants one without a settings file. It outranks
    // the saved setting for the run it is given on, as --fps= does.
    unsigned render_scale = 0;          // 0 = whatever the settings say
    unsigned render_threads = 0;        // 0 = one per core; 1 pins it to the calling thread
    bool high_resolution_text = false;  // --hi-res-text
};

[[noreturn]] void usage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s [image.bin] [--gamedir=DIR] [--install=FILE-OR-DIR] [--script=FILE] "
                 "[--call-log=FILE] [--frames=N] [--fps=N] [--time=HH:MM] "
                 "[--trace-entry=ADDR,...] [--trace-from=FRAME] [--dump-entry=ADDR:START:BYTES] "
                 "[--dump-frame=START:BYTES] "
                 "[--render-scale=1..8] [--render-threads=N] [--hi-res-text]\n",
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
    options.program_name = argc > 0 ? argv[0] : "bowling";
    const std::pair<const char*, std::string*> text_flags[] = {
        {"--gamedir=", &options.game_dir},       {"--script=", &options.script_path},
        {"--call-log=", &options.call_log_path}, {"--install=", &options.install_from},
        {"--time=", &options.fixed_time},
    };
    const std::pair<const char*, unsigned*> number_flags[] = {
        {"--frames=", &options.frame_limit},
        {"--fps=", &options.frames_per_second},
        {"--trace-from=", &options.trace_from},
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
        if (argument.rfind("--dump-frame=", 0) == 0) {
            // --dump-frame=START:BYTES[:FROM] (hex): memory printed after every frame from FROM
            // on, for comparing two builds' state frame by frame.
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
// It has no button-flags word — the `bic #0x60` signature the emulator derives that address
// from does not occur in this binary — and no press-time words, so a held Menu cannot be told
// from a tap. Presses arrive the way the firmware really delivered them: as a node on an event
// list whose head the eApp task publishes at `ctx+0x30` before each frame call. Apple's
// `postEvent` at 0x0024d918 builds the node and appends it; the game's input handler at
// 0x18007584 walks the list — `{type +0, state +1, payload +4, next +8}` — and dispatches the
// type through a six-way jump table (reference/eapp-loader/play.rs, `post_event` and the note
// beside `event_buttons`, which measured this title at its menu: 332 quads and one clear over
// 40 002 frames without a press, 930 and four with).
//
// The Lost recomp's main.cpp reads the same list; this is its model with the emulator's timing.
constexpr uint32_t CONTEXT_EVENT_LIST = 0x30;
constexpr uint32_t EVENT_NODE_SIZE = 0x10;  // the emulator's allocation; the node uses twelve
constexpr uint32_t EVENT_NODE_STATE = 0x01;
constexpr uint32_t EVENT_NODE_PAYLOAD = 0x04;
constexpr uint32_t EVENT_NODE_NEXT = 0x08;

// A press is a node with state 1 on the frame of the press; at the top of the next frame the
// emulator overwrites the same node with state 2 — its "release" — and at the top of the frame
// after that it nulls the list head. That timing is reproduced exactly, because the recordings
// were made with it and the game reads the node on every frame it is published: a node nothing
// retires is re-read as a fresh press every frame, which is what once made the Sims titles
// behave as though Select were being clicked over and over on their own.
//
// How the emulator arrives at that timing is worth spelling out, because a first reading of
// play.rs gets it a frame wrong. Its `event_hold` counter is set to 2 by every post and
// decremented once per frame *after* the script's presses have been applied — the decrement
// hangs off the keyboard's Select check, which a scripted run never takes — so a post made on
// frame N is already down to 1 by the end of N, the release posted at the top of N+1 puts it
// back to 2 and the same frame's decrement makes it 1, and N+2's decrement reaches 0 and nulls
// the head before the game is called. The controls oracle found the difference: with the
// release still published on N+2, this game reacts to the press a frame before the emulator
// does. `settle` below is that decrement, in that position.
//
// What this game makes of state 2 is *not* established here. Lost's dispatcher read it as "the
// button is still down" and started a four-second countdown to a suspend, which is why the Lost
// recomp sends a different value; whether 0x18007584 does anything like that is a question for
// the decompilation, and the emulator's value is what every recording contains.
constexpr uint8_t EVENT_STATE_PRESSED = 1;
constexpr uint8_t EVENT_STATE_RELEASED = 2;
constexpr unsigned EVENT_HOLD_FRAMES = 2;  // the emulator's `event_hold`, set by every post

// What the game makes of a node's state, from its dispatcher at 0x18007570:
//
//     0x180075d8  ldrb r0,[r4,#1] / cmp r0,#1 -> 1 / cmp r0,#2 -> 2 / else 0
//     0x18007620  (type 1, Menu)  cmp r7,#2 / movne r0,#0 / moveq r0,#1 / strb r0,[r8,#2]
//     0x18007604  (type 5, Next)  the same into [r8,#3]
//
// [r8+2] is "Menu is down" and the frame path reads it against the clock — down for over four
// seconds (0x180457a8, 0x3d0900 µs) makes the game ask to be put away, exactly what holding
// Menu does on an iPod. So **state 2 is the button going down and state 1 is it coming up**:
// the emulator has the names the other way round, and posts them in that order — up, then
// down — which is why every menu in the recordings needs two Selects (the first arms, the
// second's "up" fires) and why a Menu tap there starts the four-second countdown that nothing
// ends. The recordings were made that way and the oracle reproduces it exactly. A real run
// sends what the wheel sent: down on the frame of the press, up at the top of the next.
constexpr uint8_t EVENT_STATE_DOWN = 2;
constexpr uint8_t EVENT_STATE_UP = 1;

// The event type each button is delivered as.
//
// Menu and Previous are not where their names would put them, and that is deliberate: these
// are the types the emulator posts for each of its own buttons (play.rs, `event_type_for`
// together with the script's own mapping), and the recordings this project is compared against
// were made with them. What the game *calls* type 1 and type 3 is not established here — only
// which key produces which — so a swap would be a guess, not a fix.
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

uint8_t wheel_byte(int raw) {
    return static_cast<uint8_t>((((0x77 - raw) * 8) / 3) & 0xff);
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

    // The two states a press posts, in order: the emulator's (1 then 2) for the recordings, the
    // wheel's own (down, then up) for a real run. See EVENT_STATE_DOWN.
    void set_press_states(uint8_t first, uint8_t second) {
        press_state_ = first;
        release_state_ = second;
    }

    // A button went down. In the recordings' model the second state follows at the start of
    // the next frame (`retire_buttons`); in a real run it follows when the key comes up
    // (`lift_released`), which is how a hold reaches the game.
    void press(Button button) {
        post_event(event_type_for(button), press_state_);
        held_ = button;
        holding_ = true;
        fresh_ = true;
    }

    // A real run: the button goes up when the key does — never on the frame it went down, so the
    // game sees the down first, and only once the platform says the key is no longer held.
    void lift_released(uint32_t buttons_down) {
        if (holding_ && !fresh_ && (buttons_down & static_cast<uint32_t>(held_)) == 0) {
            post_event(event_type_for(held_), release_state_);
            holding_ = false;
        }
    }

    // The recordings' model keeps the emulator's timing: the second state at the top of the next
    // frame, held or not.
    void use_emulator_release(bool emulator) { emulator_release_ = emulator; }

    // Turn the wheel `detents` clicks (negative = the other way). Each detent is queued as its
    // own sample: the game wants to see the position *change* between polls. When the queue runs
    // dry the poll answers zero, which is the wheel reporting that nothing is touching it — the
    // release that ends a gesture. Nothing refills the queue between frames: the recordings were
    // made without the emulator's `--wheel-rotate`, and a sample the emulator did not send would
    // be a poll answer it did not give.
    void turn(int detents) {
        const int step = detents < 0 ? -1 : 1;
        for (int i = 0; i != detents; i += step) {
            raw_ = wrap_detent(raw_ + step);
            eapp::queue_input(wheel_byte(raw_));
        }
    }

    // Called first thing each frame, in the emulator's order: a button pressed last frame is
    // released now.
    void retire_buttons() {
        if (holding_ && emulator_release_) {
            post_event(event_type_for(held_), release_state_);
            holding_ = false;
        }
    }

    // Called once the frame's presses have been applied: a published node has had one more
    // frame, and one that has had its EVENT_HOLD_FRAMES comes off the list. See the note above
    // on where the emulator does this.
    void settle() {
        fresh_ = false;
        if (hold_frames_ > 0 && --hold_frames_ == 0 && context_ != 0) {
            st32(context_ + CONTEXT_EVENT_LIST, 0);  // an empty list is a null head
        }
    }

private:
    // Build the node, publish it as the whole list, and send a wheel sample alongside it — the
    // emulator's `post_event` does both, and the sample is what makes the game look at its
    // input this frame at all.
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
        hold_frames_ = EVENT_HOLD_FRAMES;
    }

    uint32_t context_ = 0;
    uint32_t node_ = 0;
    int raw_ = 0;
    Button held_ = Button::Select;
    bool holding_ = false;
    bool fresh_ = false;  // pressed this frame: not to be lifted before the game has seen it
    bool emulator_release_ = true;
    unsigned hold_frames_ = 0;
    uint8_t press_state_ = EVENT_STATE_PRESSED;
    uint8_t release_state_ = EVENT_STATE_RELEASED;
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
// before a call to say *why* it is calling, and `[ctx+0x100]` is where the game answers.
constexpr uint32_t CONTEXT_SIZE = 0x400;
constexpr uint32_t CONTEXT_ANSWER_OFFSET = 0x100;

// The reason byte is half of a handshake, and this is the first title whose pump reads the
// other half.
//
// The game's dispatcher at 0x18045740 reads `[ctx+0]`:
//
//     0 -> drain the event list, then run the FULL application init (0x180052d4)
//     1 -> the normal per-frame path (0x18045794)
//     5 -> the suspend/resume path, which answers 6
//
// and, when its init is done, writes 1 to `[ctx+0x100]` (0x1804578c). RetailOS's pump writes the
// reason only while its manager is in two of its states and leaves the byte alone otherwise, so
// the game is asked for init until it answers, then asked for frames. With a constant 0 the game
// initialises on every frame, never destroys what it built, and exhausts its 5.24 MB pool in 75
// iterations; with a constant 1 it never initialises at all. So: reason 0 while the answer byte
// is still 0, reason 1 from the frame after it is not — the emulator's `--frame-reason=auto`
// (reference/eapp-loader/play.rs, `reason_auto`), which is what the recordings are made with.
//
// The init vectors see the byte at 5, the emulator's default seed for a title that does not
// read it before the handshake begins (`ctx_seed`, `defaults_for`).
constexpr uint8_t CONTEXT_REASON_INITIALISE = 5;  // what the init vectors are called with
constexpr uint8_t CONTEXT_REASON_ASK_FOR_INIT = 0;
constexpr uint8_t CONTEXT_REASON_FRAME = 1;

// The other direction of the handshake, which the answer byte carries once the game is running.
//
// The frame path writes 5 there — "put me away" — in four places, and records *why* in a byte of
// its input-state block (0x18073854, the literal at 0x180455f4; the reason is at +7):
//
//     0x180457c8  Menu held over 4 s      -> why 3
//     0x180457ec  Next held over 2 s      -> why 4
//     0x18045828  idle past its sleep timer -> why 2
//     0x18045884  its own tick returned 0  -> why 1   (Save & Exit, Abandon Game)
//
// A frame called with reason 5 takes the lifecycle path at 0x18045890: it runs a shutdown step
// (0x18005234) each call until that returns 1, then answers 6 — "suspended" — and marks the
// context by the reason (why 2 sets ctx+0x28, 3 ctx+0x2a, 4 ctx+0x29). RetailOS would then take
// the eApp away. Left unanswered, the game sits polling its input and swapping an unchanged
// frame for ever, which is what Save & Exit looked like before this was read.
//
// So a real run answers a 5 with a 5 and ends when it hears 6; there is no iPod menu to go back
// to, so the program ends. The one request not honoured is the idle one: a player who left the
// window open does not want it closed. Under --emulator-firmware nothing is honoured, because
// the recordings were made by an emulator that honours nothing.
constexpr uint32_t CONTEXT_ANSWER_STATE = 0x00;
constexpr uint8_t CONTEXT_STATE_SUSPEND = 5;
constexpr uint8_t CONTEXT_STATE_SUSPENDED = 6;
constexpr uint32_t INPUT_STATE_BLOCK = 0x1807'3854u;
constexpr uint32_t SUSPEND_REASON_OFFSET = 7;
constexpr uint8_t SUSPEND_BECAUSE_IDLE = 2;

struct Action {
    bool quit = false;
    bool screenshot = false;
};

Action apply_script_action(const std::string& action, ClickWheel& wheel) {
    Action result;
    // "menu" is the button that opens the pause menu and "prev" the other one — the same mapping
    // play.rs uses for its scripts, so existing scripts and recordings keep their meaning.
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
        // `hold` and `spin` among them: the emulator's finger-on-the-wheel gestures, which no
        // recording of this game uses yet. Ignored loudly rather than half-done.
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
    // A held scroll key: one detent a frame for as long as it is held. The platform's
    // finger-on-a-side touches (`input.touch`) are a Lost gesture — walking — and mean nothing
    // to a bowling alley; how this game aims is a question for the first oracle that bowls.
    if (input.spin != 0) {
        wheel.turn(input.spin > 0 ? 1 : -1);
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
    // BOWLING_SHOT_DIR chooses where they go; the default suits a run from the project root.
    const char* directory = std::getenv("BOWLING_SHOT_DIR");
    char path[256];
    std::snprintf(path, sizeof path, "%s/shot-%02u.ppm", directory != nullptr ? directory : "build",
                  shot_number++);
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        std::printf("screenshot frame %u -> cannot write %s (set BOWLING_SHOT_DIR)  fnv1a %08x\n",
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
        platform::create_platform("The Sims Bowling", options.frames_per_second);
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
    host->apply_settings();
    platform::set_text_entry_supported(host->text_input_supported());

    // --trace-from=N keeps the entry trace quiet until frame N — through the init vectors too:
    // tracing every function of a 3 000-frame run to look at one frame is a gigabyte nobody reads.
    const bool tracing_asked = trace_entries_enabled();
    if (tracing_asked && options.trace_from > 0) {
        trace_entries_enabled() = false;
    }

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
    // before it had drawn a frame: the emulator measured it on this very title, whose
    // 0x18045504 ends in `__cxa_finalize` and, run at start-up, destroys the resource manager
    // and leaves every later load pushed into a null queue (play.rs, `TERMINATE_VECTOR`). It is
    // still the frame vector if it happens to be the last one; what is skipped is calling it now.
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
        storage::set_writes_refused(true);
        eapp::set_emulator_device(true);
    } else {
        wheel.set_press_states(EVENT_STATE_DOWN, EVENT_STATE_UP);
        wheel.use_emulator_release(false);
    }
    // This game re-points every attribute it wants immediately before every draw, so "pointed
    // since the last draw" is what says which attributes a draw reads — the reading Lost earned
    // and Mini Golf must not claim (gfx::set_attributes_repointed_per_draw, ../common/README.md).
    // Read from the two routines that issue every draw at the menu:
    //
    //   0x180424a0  the sprite-batch flush: `#137` attribute 0 (positions, [batch+0x70]) and
    //               `#40`, then attribute 1 (uv, [batch+0x74]) and `#40`, attribute 2 (colours,
    //               [batch+0x78]) pointed and enabled only when the batch is coloured and
    //               `#36`-disabled otherwise, then `#38 glDrawElements(TRIANGLE_STRIP, ...)`.
    //   0x18042450  the flat rectangle: `#137` attribute 0 and `#40`, pipeline 1 (`#159`), the
    //               constant colour from a stack vector (`#148`), `#37 glDrawArrays(QUADS, 0, 4)`.
    //               Attribute 1 is neither pointed nor disabled: pipeline 1 does not read it.
    //
    // Under the conservative reading the flat rectangle — the menu's selection bar — sampled the
    // previous batch's texture through attribute 1's stale pointer and came out as a smear. The
    // emulator reads it the same way, which is why the picture oracle (which runs under
    // `--emulator-graphics`, where the flag keeps the emulator's reading) never saw it.
    gfx::set_attributes_repointed_per_draw(true);
    gfx::set_emulator_graphics(options.emulator_graphics);
    // How many threads draw a frame. Not a setting about the picture — every frame is
    // bit-for-bit what one thread would have drawn (framework/graphics.h) — so the oracles
    // run with it too, and it is here only so that something can be measured against one.
    gfx::set_render_threads(options.render_threads);

    // A quit (from the script or the window) ends the loop at the top of the *next* frame: the
    // frame that saw the quit still runs, as in the emulator, so the two logs end at the same call.
    bool quit_requested = false;
    // The reason the *game* asked for, carried to the next call. See CONTEXT_STATE_* above.
    uint8_t next_reason = CONTEXT_REASON_FRAME;
    for (unsigned frame = 0;
         !quit_requested && (options.frame_limit == 0 || frame < options.frame_limit); ++frame) {
        eapp::call_log().begin_frame(frame);
        if (tracing_asked && frame == options.trace_from) {
            trace_entries_enabled() = true;
        }
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
        wheel.lift_released(input.buttons_down);
        wheel.settle();

        deliver_completions(cpu);

        // A screenshot asked for on frame N is what is on screen when frame N *begins* — the
        // emulator's `shot` is handled before its frame call (play.rs), so a shot of frame N
        // shows frame N-1's drawing, and the picture oracle compares against that. It made no
        // difference on the static screens the earlier titles were compared on; on this game's
        // power meter, which moves every frame, a shot taken after the call was one meter step
        // ahead of the emulator's and read as a rendering fault (tests/frames.sh first-frame).
        if (action.screenshot || live.screenshot) {
            save_screenshot(frame);
        }

        // Why the firmware is calling: the handshake of CONTEXT_REASON_* above. "Answered" is
        // the answer byte no longer being the zero the context was allocated with.
        const bool initialised = ld8(context + CONTEXT_ANSWER_OFFSET) != 0;
        st8(context, initialised ? next_reason : CONTEXT_REASON_ASK_FOR_INIT);
        call_guest(cpu, frame_vector, {context, context + CONTEXT_ANSWER_OFFSET, 0, 0});

        // What the game asked to be called with next. See CONTEXT_STATE_* above.
        const auto answered =
            static_cast<uint8_t>(ld8(context + CONTEXT_ANSWER_OFFSET + CONTEXT_ANSWER_STATE));
        next_reason = CONTEXT_REASON_FRAME;
        if (!options.emulator_firmware) {
            if (answered == CONTEXT_STATE_SUSPEND &&
                ld8(INPUT_STATE_BLOCK + SUSPEND_REASON_OFFSET) != SUSPEND_BECAUSE_IDLE) {
                next_reason = CONTEXT_STATE_SUSPEND;
            } else if (answered == CONTEXT_STATE_SUSPENDED) {
                std::printf("the game asked to be put away (reason %u); ending\n",
                            ld8(INPUT_STATE_BLOCK + SUSPEND_REASON_OFFSET));
                quit_requested = true;
            }
        }

        forward_audio_requests(*host, options.game_dir);
        report_frame_dumps(frame);
        // Not before the game has drawn anything. Its first frames draw nothing — the firmware
        // is asking it to initialise — and the buffer is still the magenta that marks an
        // un-drawn region, which the window would otherwise show as the game's opening screen.
        if (gfx::anything_drawn()) {
            host->present(gfx::screen_pixels(), gfx::screen_width(), gfx::screen_height());
        }
        host->wait_for_next_frame();
    }
    return EXIT_SUCCESS;
}

}  // namespace
}  // namespace bowling

int main(int argc, char** argv) {
    // On Windows this is a windowed program with no console of its own: it joins the terminal
    // that started it, if there was one, and otherwise says anything fatal in a message box.
    // Everywhere else, and in the headless build, it does nothing at all.
    bowling::platform::windows_console_begin("The Sims Bowling");
    return bowling::run(bowling::parse_options(argc, argv));
}
