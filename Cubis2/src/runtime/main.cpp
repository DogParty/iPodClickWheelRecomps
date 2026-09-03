// The program: load the game, run its init vectors, then pump frames until told to stop.
//
// This is the recomp's equivalent of the iPod firmware's eApp task, and it copies the emulator's
// frame pump (reference/eapp-loader/play.rs) step for step, because the verification oracle
// compares the two runs call for call. Where the emulator's behaviour was a measured fact about
// the firmware it is cited; where it was the emulator's own convention (scratch allocations,
// the order of input delivery within a frame) it is copied anyway, since the recorded logs
// depend on it.
//
//   cubis          [--script=FILE] [--fps=N]
//   cubis-headless <image.bin> --gamedir=DIR [--script=FILE] [--call-log=FILE] [--frames=N]
//
// Without --gamedir the game's files are looked for in the platform's data directory
// (platform/paths.h) and, the first time, installed there from the folder or zip the player
// picks (gamedata/install.h). With it — the oracle tests — the directory is used as given and
// nothing is checked or copied.
//
// Ported from the Vortex recomp's main.cpp (reference/PORTED.md). Three parts are this title's
// own and are marked below: how a button press reaches the game (a flags word, at this image's
// address), what the reason byte does — this game reads it at `[ctx+0x100]`, which nothing in
// the pump ever writes (PLAN.md differences 1 and 2) — and how the game's clock advances from
// one frame to the next (difference 5).
#include "framework/graphics.h"
#include "framework/music_library.h"
#include "framework/storage.h"
#include "gamedata/install.h"
#include "gamedata/manifest.h"
#include "ipod/platform/device.h"
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
#include <chrono>
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

namespace cubis {
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
    std::string fixed_time;
    std::string program_name;  // argv[0], for a message that says how to run it again
    // --battery=N: report N% charge instead of the host's, so a gauge can be seen at a level
    // this machine is not at. Empty = the host's (ipod/platform/device.h).
    std::string fixed_battery;  // HH:MM shown by the game's clock, for reproducible runs
    unsigned frame_limit = 0;   // stop after this many frames; 0 = run until quit
    unsigned trace_from = 0;    // --trace-from=N: the entry trace is silent before frame N
    // Behave as the emulator's own harness did — step the game's clock per call rather than per
    // frame, refuse the game's file writes, answer the store "not ready". Only the oracle wants
    // this; see tests/diff.sh.
    bool emulator_firmware = false;
    // Render the flat-fill pipeline as the emulator does, which is the one rendering decision
    // the two make differently. Only the picture oracle wants this; see tests/frames.sh.
    bool emulator_graphics = false;
    // The rate the device ran this game at, and the least its arithmetic can take (the clock,
    // below). The saved setting normally decides; this is what --fps= overrides it with.
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
                 "[--call-log=FILE] [--frames=N] [--fps=N] [--time=HH:MM] [--battery=N] "
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
    options.program_name = argc > 0 ? argv[0] : "cubis";
    const std::pair<const char*, std::string*> text_flags[] = {
        {"--gamedir=", &options.game_dir},       {"--script=", &options.script_path},
        {"--call-log=", &options.call_log_path}, {"--install=", &options.install_from},
        {"--time=", &options.fixed_time},        {"--battery=", &options.fixed_battery},
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
// The game keeps a word of button flags and tests its bits every frame — the model Mini Golf,
// Texas Hold'em and Vortex use, which is no surprise: build 2563292 sits between Hold'em's
// 2563291 and Mini Golf's 2563296, one SDK and one week. Bits 0x01..0x10 are the five buttons;
// the press sets the bit and the next frame clears it, so one frame with the bit set is one
// press. The word's address is what the emulator's `find_flags_word` derives from the
// `bic r0,r0,#0x60` signature in the image and reports at start-up ("button flags word at
// 0x180a9db0"; reference/eapp-loader/play.rs, `press_button`). Hold'em's is at 0x180597a8,
// Vortex's at 0x18063e5c and Mini Golf's at 0x18037a0c — every title puts it somewhere
// different.
//
// The **wheel** does not come this way. The game's frame vector polls `InputEvents #0` itself —
// `0x18038568: mov r0,sp / add r1,sp,#4 / bl 0x18000920`, then `ldr r0,[sp,#4]` and
// `tst r0,#0x40000000` at `0x1803857c` — and dispatches the low byte through `0x180054d4`. That
// byte is the wheel sample the emulator's queue hands back (`Stub::InputPoll`), so a press has
// to queue one alongside the flag bit or the game never looks at the frame at all; the pump
// below does exactly what `press_button` does, for that reason.
//
// There are no press-time words here: the emulator reports "no press-time words for this
// title", so a held Menu cannot be told from a tap the way Mini Golf's can. Whether this game
// watches for a long press at all, and where it would keep the time, is not established.
constexpr uint32_t BUTTON_FLAGS_ADDRESS = 0x180a'9db0u;

// When Menu and Next went down, which is how this game tells a tap from a hold.
//
// The emulator reports "no press-time words for this title" — it only knows Mini Golf's, which
// were measured by hand — but this game has them, and its own lifecycle routine says where. At
// `0x180386c8`, with `r9` = the input-state block (`0x180a9d9c`, the literal at `0x18038758`,
// whose `+0x14` is the button-flags word above) and `r0` = the microsecond clock the firmware
// left at `[ctx+4]`:
//
//   0x180386c8  ldr r1,[r9,#0x14] / and r1,r1,#0x10   ; is MENU down?
//   0x180386d8  ldrne r1,[r9,#0x18]                   ; when it went down
//   0x180386dc  subne r0,r0,r1                        ; how long it has been down
//   0x180386e0  ldrne r1,[r9,#4] / cmpne r0,r1        ; longer than the limit?
//   0x180386e8  strbhi sl,[r9] / strbhi r2,[r5]       ; then write 5 into [ctx+0]: put me away
//
// and the same shape for Next (bit 0x08, `+0x1c` against `+0x8`, answering 2).
//
// Left at zero, "when it went down" is the beginning of time, so **every** Menu press read as a
// hold of the whole session and the game asked to be shut down — from the main menu, from the
// pause menu, from inside a level. Menu could not do its own job of stepping back a screen even
// once. Stamping the press with the clock the game itself is about to read makes a tap a tap;
// a key genuinely held still crosses the limit, because the stamp is only written on the frame
// the press begins.
constexpr uint32_t PRESS_TIME_MENU = 0x180a'9db4u;
constexpr uint32_t PRESS_TIME_NEXT = 0x180a'9db8u;

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
    // frame — the game only looks at its buttons on a frame whose poll reported an event. The bit
    // is cleared at the start of the next frame by `retire_buttons`.
    void press(Button button) {
        const uint32_t bit = static_cast<uint32_t>(button);
        st32(BUTTON_FLAGS_ADDRESS, ld32(BUTTON_FLAGS_ADDRESS) | bit);
        eapp::queue_input(wheel_byte(raw_));
        // Only on the frame the press *begins*: a key held down keeps its original stamp, so the
        // game can still see a real hold. See PRESS_TIME_MENU.
        if ((held_ & bit) == 0) {
            stamp_press(button);
        }
        held_ |= bit;
    }

    // Hold a button down and keep it down: the bit stays set across frames and its press time
    // keeps the stamp from the frame the hold began, so the game's own hold test can elapse.
    // This is how the window's close button reaches a game that only saves when it is asked to
    // shut down — the ask is a *held* Menu, and a tap is now correctly a tap (PRESS_TIME_MENU).
    void hold(Button button) {
        const uint32_t bit = static_cast<uint32_t>(button);
        if ((sticky_ & bit) == 0) {
            press(button);
            sticky_ |= bit;
            return;
        }
        st32(BUTTON_FLAGS_ADDRESS, ld32(BUTTON_FLAGS_ADDRESS) | bit);
        eapp::queue_input(wheel_byte(raw_));
        held_ |= bit;
    }

    // The context, for the clock the press times are measured against — `[ctx+4]`, which the
    // frame vector fills from miscTBD #9 and the hold test at 0x180386c8 reads. One frame stale
    // by the time a press is stamped, against a limit measured in seconds.
    void set_context(uint32_t context) { context_ = context; }

    // The emulator never wrote these words — it reports "no press-time words for this title",
    // knowing only Mini Golf's — so every recording in tests/expected/ is of a run where they
    // stayed zero. `--emulator-firmware` keeps them that way; a real run stamps them.
    void set_press_times_stamped(bool stamped) { stamp_ = stamped; }

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

    // Called first thing each frame: last frame's presses are over.
    void retire_buttons() {
        const uint32_t over = held_ & ~sticky_;  // a held button is not over
        if (over != 0) {
            st32(BUTTON_FLAGS_ADDRESS, ld32(BUTTON_FLAGS_ADDRESS) & ~over);
        }
        held_ &= sticky_;
    }

private:
    void stamp_press(Button button) {
        if (context_ == 0 || !stamp_) {
            return;
        }
        const uint32_t at = button == Button::Menu   ? PRESS_TIME_MENU
                            : button == Button::Next ? PRESS_TIME_NEXT
                                                     : 0;
        if (at != 0) {
            st32(at, ld32(context_ + 4));
        }
    }

    int raw_ = 0;
    uint32_t held_ = 0;
    uint32_t sticky_ = 0;  // buttons `hold` is keeping down
    uint32_t context_ = 0;
    bool stamp_ = true;
};

// The emulator allocates one sixteen-byte event node from the game's heap after the init
// vectors, for every title, whether or not its buttons travel that way (play.rs). This title's
// do not — they are the flags word above — but the block is still taken, in the same place and
// at the same moment, so that every later heap address agrees with the recordings.
constexpr uint32_t EVENT_NODE_SIZE = 0x10;

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
// before a call to say *why* it is calling, and `[ctx+0x100]` is where a game may answer.
constexpr uint32_t CONTEXT_SIZE = 0x400;
constexpr uint32_t CONTEXT_ANSWER_OFFSET = 0x100;

// The reason byte, for this title: seeded once, never written again, and never read.
//
// A binary named `Cubis2*` matches no arm of the emulator's per-title table and takes its last
// one (`_ => d(true, None, None, None)`, reference/eapp-loader/play.rs `defaults_for`): a seed
// of 5, no frame reason, no pump mark. With all three of those absent the emulator's pump pokes
// `[ctx+0]` once before the init vectors and then writes neither byte again for the life of the
// run — the model Mini Golf's pump uses.
//
// **This game reads the other byte.** Its frame vector takes the reason from `[ctx+0x100]`, not
// from `[ctx+0]`:
//
//   0x1803852c  push {...} / mov r4,r0        ; r0 = ctx
//   0x18038538  mov r5,r1                     ; r1 = ctx+0x100
//   0x18038540  ldrb r0,[r5]                  ; the reason, from ctx+0x100
//   0x1803854c  cmp r0,#0                     ; zero takes the arm at 0x18038550..0x1803856c
//
// and nothing in either the firmware model or the game writes that byte, so it holds the zero
// its allocation was cleared to for every frame of every run. The seed of 5 at `[ctx+0]` is
// therefore inert here — it is kept because the emulator writes it and the exact oracle compares
// the memory the vectors are handed, not because the game consults it. Sudoku is the only other
// title known to read the reason at `+0x100` (play.rs, `--reason-offset`); six titles, six
// protocols. PLAN.md difference 1.
constexpr uint8_t CONTEXT_REASON = 5;

// The same byte is where a game says what it is *doing*, and 6 means "put me away".
//
// **Inherited, not established here.** This is Vortex's and Mini Golf's rule, kept because it is
// the shape of the pump and removing it would leave a real run with no way out at all; but the
// evidence for it is those titles' — Vortex was watched writing 1, then 5, then 6 across a Menu
// press on its main menu. Cubis 2 has not been watched doing anything of the kind, and it never
// *reads* `[ctx+0]` (see `CONTEXT_REASON`), so whether it writes it is a question this tree has
// not answered. `--dump-frame=<ctx>:0x20` across a Menu press on its main menu is the
// measurement; PLAN.md's "not verified" list carries it until then.
//
// If it does not write the byte, this costs nothing: the loop simply never sees a 6. What it
// would cost is a *different* way out going unnoticed, which is why the question stays open
// rather than being closed by silence.
//
// The emulator does neither: it kept calling a suspended game to the end of the script, and the
// recordings in tests/expected/ are of that. So the oracle keeps the old behaviour
// (--emulator-firmware) and a real run stops.
constexpr uint8_t CONTEXT_STATE_SUSPENDED = 6;

// The game's clock, from one frame to the next (PLAN.md difference 5).
//
// The clock must advance once per *frame* rather than once per call, or a game that reads it
// several times a frame — this one reads `miscTBD #12` around its logic and around its render —
// measures a frame several times longer than the one it just ran and its own timers run fast.
// That much is common to every title here.
//
// The floor below is Vortex's, and is kept for the same reason its suspend rule is: it is free
// where it is not needed. Vortex divides by its own frame delta and faults outright on a frame
// shorter than a 64th of a second (`0x18010aa4`), which is why the emulator paces *that* title
// at 30 fps; a binary named `Cubis2*` gets no `fps` from the emulator's table at all, so nothing
// says this game shares that arithmetic, and nothing says it does not. No divide-by-zero has
// been seen here in any run — but every run so far has been under `--fixed-clock`, where the
// question cannot arise, so that is not evidence. PLAN.md's "not verified" list carries it.
//
// So: at a locked rate the frame is its interval, exactly; unlocked, it is the time the last
// frame really took, floored. Under --emulator-firmware none of this runs and the clock steps
// per call, as the recordings were made.
constexpr uint32_t MICROSECONDS_PER_SECOND = 1'000'000;
constexpr uint32_t SHORTEST_FRAME_MICROSECONDS = MICROSECONDS_PER_SECOND / 64;

class FrameClock {
public:
    // How long the frame about to run lasts, as the game will see it.
    uint32_t next_frame_microseconds(unsigned frames_per_second) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(now - last_frame_).count();
        last_frame_ = now;
        const uint32_t paced = frames_per_second == 0 ? SHORTEST_FRAME_MICROSECONDS
                                                      : MICROSECONDS_PER_SECOND / frames_per_second;
        const uint32_t measured =
            elapsed <= 0
                ? 0u
                : static_cast<uint32_t>(std::min<long long>(elapsed, MICROSECONDS_PER_SECOND));
        return std::max(paced, std::max(measured, SHORTEST_FRAME_MICROSECONDS));
    }

private:
    std::chrono::steady_clock::time_point last_frame_ = std::chrono::steady_clock::now();
};

struct Action {
    bool quit = false;   // stop now, as a recording's `quit` does
    bool close = false;  // the window was closed: ask the game to save and shut down first
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
    } else if (action == "close") {
        // What closing the window does, scriptable so the shutdown path can be tested without
        // one — `tests/unit/…` cannot open a window and the oracle cases must not.
        result.close = true;
    } else {
        // `hold`, `tap` and `spin` among them: the emulator's finger-on-the-wheel gestures,
        // which no recording of this game uses yet. Ignored loudly rather than half-done.
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
    // A held scroll key: one detent a frame for as long as it is held. That is the game's own
    // "wheel rotation" control scheme; its other scheme, "finger position", wants an absolute
    // position on the wheel, which the desktop has no input for yet (PLAN.md, Not today).
    if (input.spin != 0) {
        wheel.turn(input.spin > 0 ? 1 : -1);
    }
    Action out;
    out.quit = input.quit;
    out.screenshot = input.screenshot;
    return out;
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
    // CUBIS_SHOT_DIR chooses where they go; the default suits a run from the project root.
    const char* directory = std::getenv("CUBIS_SHOT_DIR");
    char path[256];
    std::snprintf(path, sizeof path, "%s/shot-%02u.ppm", directory != nullptr ? directory : "build",
                  shot_number++);
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        std::printf("screenshot frame %u -> cannot write %s (set CUBIS_SHOT_DIR)  fnv1a %08x\n",
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
        platform::create_platform("Cubis 2", options.frames_per_second);
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
    if (!options.fixed_battery.empty()) {
        ipod::platform::set_fixed_battery_percent(std::atoi(options.fixed_battery.c_str()));
    }

    guest_memory_init();
    const EAppImage image = load_eapp_image(options.image_path.c_str());
    if (!options.call_log_path.empty()) {
        eapp::call_log().open(options.call_log_path.c_str());
    }
    eapp::set_game_directory(options.game_dir);

    // Where the game's own files go. The platform may have somewhere of its own; otherwise they
    // sit beside the game, which is where the iPod kept them and where the tests look for them.
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

    if (options.emulator_firmware) {
        eapp::set_clock_advances_per_call(true);
        eapp::set_emulator_device(true);
        storage::set_store_stubbed(true);
        storage::set_writes_refused(true);
    }
    // Two blocks come out of the game's heap before the game itself allocates anything: the
    // music library's now-playing playlist (framework/music_library.h) and this context. Their
    // order and size decide where the game's own first allocation lands, and the oracle compares
    // those addresses, so both are taken here and in this order.
    music::reserve_playlist();
    const uint32_t context = eapp::heap().alloc(CONTEXT_SIZE);
    st8(context, CONTEXT_REASON);  // once; see CONTEXT_REASON

    // Init vectors run once, in order; the last one is the per-frame callback. Slot 1 is the
    // terminate entry and is *not* an initialiser — running it here would take the game down
    // before it had drawn a frame (the emulator measured it on The Sims Bowling, whose terminate
    // vector ends in `__cxa_finalize`; play.rs, `TERMINATE_VECTOR`). It is still the frame vector
    // if it happens to be the last one; what is skipped is calling it now.
    for (const EAppVector& vector : image.vectors) {
        if (vector.slot == TERMINATE_VECTOR_SLOT) {
            continue;
        }
        call_guest(cpu, vector.address, {context, context + CONTEXT_ANSWER_OFFSET, 0, 0});
    }
    const uint32_t frame_vector = image.vectors.back().address;

    // The emulator's event node (EVENT_NODE_SIZE): unused by this title, taken for the addresses.
    const uint32_t event_node = eapp::heap().alloc(EVENT_NODE_SIZE);
    (void)event_node;

    const std::vector<ScriptStep> script = load_script(options.script_path);
    size_t next_step = 0;
    ClickWheel wheel;
    wheel.set_context(context);  // the clock a press time is measured against
    wheel.set_press_times_stamped(!options.emulator_firmware);
    FrameClock frame_clock;
    // Which attributes a draw reads. This game points its attributes immediately before every
    // draw — the boot log has `#137`/`#40` three times for each `#37`, one pair per attribute —
    // so "pointed since the last draw" is the reading (gfx::set_attributes_repointed_per_draw,
    // ../common/README.md). Read from the log; the picture oracle is what confirms it.
    gfx::set_attributes_repointed_per_draw(true);
    gfx::set_emulator_graphics(options.emulator_graphics);
    // How many threads draw a frame. Not a setting about the picture — every frame is
    // bit-for-bit what one thread would have drawn (framework/graphics.h) — so the oracles
    // run with it too, and it is here only so that something can be measured against one.
    gfx::set_render_threads(options.render_threads);

    // A quit (from the script or the window) ends the loop at the top of the *next* frame: the
    // frame that saw the quit still runs, as in the emulator, so the two logs end at the same call.
    bool quit_requested = false;
    // Closing the window must not throw the player's game away.
    //
    // This game writes both of its saves — `cubissave.dat` and `cubisgame.dat`, through the store
    // ordinals — only when it is *asked to shut down*, and the way to ask is the way a player
    // asks on the device: Menu on the main menu. Watched with CUBIS_TRACE_FILES=1, a Menu press
    // there produces `store-open`/`store-write`/`store-close` for both files and then the game
    // sets its suspend byte and stops. Ending the loop the moment the window closed skipped all
    // of that, and every session's high scores and settings were lost — which is what "the save
    // isn't saving" was.
    //
    // So a quit from the *host* does not stop the loop. It starts a shutdown: Menu once a frame,
    // which walks the game back out of whatever screen it is on and then out of the game
    // altogether, until it answers with CONTEXT_STATE_SUSPENDED or the budget runs out. A quit
    // from a *script* still stops at once — a recorded case has to end where the recording did.
    unsigned shutting_down = 0;
    // Long enough for the game to close a menu or two and write 31 KB, short enough that a game
    // that will never answer cannot hold the window open: ten seconds at the rate it runs.
    constexpr unsigned SHUTDOWN_FRAMES = 300;
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
            action.close |= step.close;
            action.screenshot |= step.screenshot;
            ++next_step;
        }
        platform::FrameInput input;
        host->poll(input);
        platform::text_entry_deliver(input.typed);
        const Action live = apply_platform_input(input, wheel);
        // The script's quit is immediate; the window's asks the game to put itself away first.
        quit_requested = action.quit;
        if ((live.quit || action.close) && shutting_down == 0) {
            shutting_down = SHUTDOWN_FRAMES;
            std::puts("closing: asking the game to save and shut down");
        }
        if (shutting_down != 0) {
            // *Held*, not tapped: a tap on the main menu steps back a screen, and asking to be
            // put away is what a hold means (PRESS_TIME_MENU).
            wheel.hold(Button::Menu);
            if (--shutting_down == 0) {
                std::puts("closing: the game did not answer; stopping anyway");
                quit_requested = true;
            }
        }

        deliver_completions(cpu);

        // A screenshot asked for on frame N is what is on screen when frame N *begins* — the
        // emulator's `shot` is handled before its frame call (play.rs), so a shot of frame N
        // shows frame N-1's drawing, and the picture oracle compares against that.
        if (action.screenshot || live.screenshot) {
            save_screenshot(frame);
        }

        // The frame's length, as the game will measure it (FrameClock). Nothing under
        // --emulator-firmware: there the clock steps per call.
        eapp::advance_clock(frame_clock.next_frame_microseconds(platform::settings().frame_rate));
        call_guest(cpu, frame_vector, {context, context + CONTEXT_ANSWER_OFFSET, 0, 0});

        forward_audio_requests(*host, options.game_dir);
        report_frame_dumps(frame);
        // The game has asked to be put away (CONTEXT_STATE_SUSPENDED): stop, rather than call a
        // game that will never draw again.
        if (!options.emulator_firmware && ld8(context) == CONTEXT_STATE_SUSPENDED) {
            if (shutting_down != 0) {
                std::puts("closing: the game saved and shut down");
            }
            quit_requested = true;
        }
        // Not before the game has drawn anything: the buffer is still the magenta that marks an
        // un-drawn region, which the window would otherwise show as the game's opening screen.
        if (gfx::anything_drawn()) {
            host->present(gfx::screen_pixels(), gfx::screen_width(), gfx::screen_height());
        }
        host->wait_for_next_frame();
    }
    return EXIT_SUCCESS;
}

}  // namespace
}  // namespace cubis

int main(int argc, char** argv) {
    // On Windows this is a windowed program with no console of its own: it joins the terminal
    // that started it, if there was one, and otherwise says anything fatal in a message box.
    // Everywhere else, and in the headless build, it does nothing at all.
    cubis::platform::windows_console_begin("Cubis 2");
    return cubis::run(cubis::parse_options(argc, argv));
}
