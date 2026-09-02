// The miscTBD and Settings frameworks: memory, clock, device level, resource names, settings.
//
// "miscTBD" is the name the reverse-engineering gave the grab-bag framework that every title
// imports: the heap, the microsecond clock, the backlight/volume level, the wall clock, the
// battery, and the resource-name resolver. Each ordinal's behaviour is the emulator's
// (reference/eapp-loader/lib.rs, the Stub variant named in each comment).
//
// Two things here are this title's own. `#2` is `realloc`, which no earlier title called and
// this one calls two thousand times in a boot (PLAN.md difference 4). And the clock has two ways
// of advancing (difference 5): the emulator's fixed step per *call*, which the recordings were
// made with, and a step per *frame* for a real run — because this game reads the clock several
// times a frame and divides by the difference between frames, so a per-call step would run it
// several times too fast, and a frame shorter than a 64th of a second would divide by zero.
#include "framework/device.h"
#include "heap.h"
#include "host_state.h"
#include "ipod_eapp.h"
#include "ipod/platform/device.h"
#include "runtime/memory.h"

#include <algorithm>
#include <ctime>
#include <string>

namespace cubis::eapp {

namespace {

// The emulator's timebase under `--fixed-clock`: miscTBD #9 advances this much per call
// (reference/eapp-loader/play.rs, `Stub::Clock { step: 16_667 }`). This is what every recording
// in tests/expected/ was made with, so it is what `--emulator-firmware` reproduces.
constexpr uint32_t CLOCK_STEP_PER_CALL_MICROSECONDS = 16'667;

// Settings #0 error codes, as the firmware's settings dispatcher reports them.
constexpr uint32_t SETTING_BAD_ARGUMENT = static_cast<uint32_t>(-49);
constexpr uint32_t SETTING_UNKNOWN = static_cast<uint32_t>(-50);

constexpr uint32_t RESOLVED_NAME_CAPACITY = 80;

struct MiscState {
    uint32_t clock_microseconds = 0;
    // Whether the clock steps on every call (the emulator's recording model) or only when the
    // frame pump advances it (`advance_clock`, a real run). Per frame by default: the oracle
    // asks for the other with --emulator-firmware, and a run that forgot would be merely fast,
    // whereas a recording compared against the wrong model would never match.
    bool advances_per_call = false;
    // Report the clock and the battery the way the *emulator* did, for the recordings' sake:
    // an hour already folded to 12 and a call that answers 0, a battery pinned full. See
    // `wall_clock` and `battery_level`; --emulator-firmware turns it on and nothing else should.
    bool emulator_device = false;
    uint32_t device_level = 0;  // 0..100, whatever the game last set
    std::string pending_resource_name;
    uint32_t language = 0;  // the Settings "Language" value; 0 is English
};

MiscState& misc() {
    static MiscState instance;
    return instance;
}

}  // namespace

std::string take_pending_resource_name() {
    std::string name;
    name.swap(misc().pending_resource_name);
    return name;
}

// Pin the wall clock, so a replay draws the same clock digits whenever it is run. The clock
// itself is the shared core's (ipod/platform/device.h), because it is a fact about the host and
// not about this binary, and because every title had its own wrong copy of it.
void set_fixed_host_time(int hour, int minute) {
    ipod::platform::set_fixed_local_time(hour, minute);
}

// Answer #12 and #13 as the emulator's stubs did, for the recordings in tests/expected/.
void set_emulator_device(bool emulator) {
    misc().emulator_device = emulator;
}

void set_clock_advances_per_call(bool per_call) {
    misc().advances_per_call = per_call;
}

void advance_clock(uint32_t microseconds) {
    if (!misc().advances_per_call) {
        misc().clock_microseconds += microseconds;
    }
}

}  // namespace cubis::eapp

namespace cubis::device {

// The implementation lives in the file above.
using namespace cubis::eapp;  // NOLINT(google-build-using-namespace): one file, by design

// #0 alloc(size) -> pointer, 0 on exhaustion.  (Stub::Alloc)
GuestAddress allocate(uint32_t bytes) {
    log_call("miscTBD", 0, {bytes});
    return heap().alloc(bytes);
}

// #1 free(pointer).  (Stub::Free)
void release(GuestAddress memory) {
    log_call("miscTBD", 1, {memory});
    heap().free(memory);
}

// #2 realloc(pointer, size) -> pointer.  (Stub::Realloc { ptr: 0, size: 1 })
//
// The argument order was *measured* on this title rather than read: the engine's wrapper at
// 0x18000fac tail-calls through 0x18020d90 and what arrives at the import is (old, size) — with
// that order the string table's keys come out as 602, 603, 700; with the other they come out
// 0, 1, 2 (reference/eapp-loader/lib.rs, the comment on the stub). The rules of the resize
// itself are the shared heap's (ipod/libeapp/heap.h).
GuestAddress reallocate(GuestAddress memory, uint32_t bytes) {
    log_call("miscTBD", 2, {memory, bytes});
    return heap().realloc(memory, bytes);
}

// #9 clock(out) -> microseconds; also stored at *out.  (Stub::Clock)
//
// Per call the step is the emulator's; per frame the pump has already advanced the clock and
// each call within the frame reads the same moment, one microsecond later — strictly
// monotonic, as the emulator's wall clock is (`lib.rs`, the `next = real.max(clock + 1)` rule),
// because the game also subtracts consecutive readings within a frame.
uint32_t clock_microseconds(GuestAddress out) {
    log_call("miscTBD", 9, {out});
    misc().clock_microseconds += misc().advances_per_call ? CLOCK_STEP_PER_CALL_MICROSECONDS : 1;
    st32(out, misc().clock_microseconds);
    return misc().clock_microseconds;
}

// #12 wall clock into six words at *out: second, minute, **hour 0..23**, day, month, year;
// answers non-zero when it filled them in.  (Stub::HostTime)
//
// Both of those were wrong in every recomp before this one, and this game's own code says so.
// Its consumer at `0x1800de30` does:
//
//   0x1800de48  mov r4,#0xc / mov r6,#0 / mov r5,#1   ; the defaults: 12, :00, PM
//   0x1800de58  bl 0x18005694                          ; miscTBD #12
//   0x1800de5c  cmp r0,#0 / beq 0x1800deac             ; ANSWERED 0 -> keep the defaults
//   0x1800de64  ldr r0,[sp,#0x1c]                      ; word[2] = the hour
//   0x1800de68  cmp r0,#0xc / bls …                    ; > 12 -> hour-12, PM
//   0x1800de7c  cmp r0,#0xc / beq …                    ; == 12 -> 12, PM
//   0x1800de94  cmp r4,#0 / …                          ; == 0  -> 12, AM
//   0x1800dea8  ldr r6,[sp,#0x18]                      ; word[1] = the minute
//
// So (a) a call that returns 0 is a call that *failed*, and the game falls back to a hard-coded
// **12:00 PM** — which is what this port's status bar showed at every hour of the day, because
// the function returned void; and (b) the game does the 12-hour conversion itself from a 24-hour
// value, so handing it one already folded made every afternoon read as a morning and midnight
// read as noon.
//
// The clock comes from the shared core now (ipod/platform/device.h). Under --emulator-firmware
// the old shape is put back, because the recordings in tests/expected/ were made against an
// emulator whose stub had both faults.
uint32_t wall_clock(GuestAddress out) {
    log_call("miscTBD", 12, {out});
    if (out == 0) {
        return 0;
    }
    const ipod::platform::LocalTime now = ipod::platform::local_time_now();
    const int hour = misc().emulator_device
                         ? (now.hour % 12 == 0 ? 12 : now.hour % 12)
                         : now.hour;
    const int fields[6] = {now.second, now.minute, hour, now.day, now.month, now.year};
    for (uint32_t i = 0; i < 6; ++i) {
        st32(out + 4 * i, static_cast<uint32_t>(fields[i]));
    }
    return misc().emulator_device ? 0u : 1u;
}

// #10 answers 1000 — the rate the game divides clock readings by.  (Stub::Value)
uint32_t clock_rate() {
    log_call("miscTBD", 10, {});
    return 1000;
}

// #11 answers 0.  (Stub::Value(0))
uint32_t clock_reserved() {
    log_call("miscTBD", 11, {});
    return 0;
}

// #13 battery level in fifths (0..20), from a percentage.  (Stub::HostBattery)
//
// The charge is the host's, read by the shared core through whatever its platform offers
// (ipod/platform/device.h) — a machine with no battery reports full, which is what a device on
// the charger reports. Every recomp before this one returned a hard-coded 100 here, so the gauge
// was painted full on a laptop at 5 %. --emulator-firmware puts the constant back, because that
// is what the recordings hold.
uint32_t battery_level() {
    log_call("miscTBD", 13, {});
    const uint32_t percent = misc().emulator_device ? 100u : ipod::platform::battery_percent();
    return (percent * 20 + 50) / 100;
}

// #6 and #5: the device level the game only ever stores and reads back (the backlight).
uint32_t brightness() {
    log_call("miscTBD", 6, {});
    return misc().device_level;
}

void set_brightness(uint32_t level) {
    log_call("miscTBD", 5, {level});
    misc().device_level = std::min<uint32_t>(level, 100);
}

// #7 the idle notice: the hardware took it and answered 0.  (Stub::ReturnZero)
void set_idle_inhibited(uint32_t inhibited) {
    log_call("miscTBD", 7, {inhibited});
}

// #14 resolve a resource name: `name` names the resource, `descriptor` receives two words
// (0, 8) and the name itself at +8. The name is remembered for the next stream registered.
// (Stub::ResolveName)
void resolve_resource(uint32_t reserved, GuestAddress descriptor, uint32_t flags,
                      GuestAddress name) {
    log_call("miscTBD", 14, {reserved, descriptor, flags, name});
    const std::string resolved = read_guest_string(name, 64);
    if (descriptor != 0) {
        st32(descriptor, 0);
        st32(descriptor + 4, 8);
        const uint32_t length =
            std::min<uint32_t>(static_cast<uint32_t>(resolved.size()), RESOLVED_NAME_CAPACITY);
        for (uint32_t i = 0; i < length; ++i) {
            st8(descriptor + 8 + i, static_cast<uint8_t>(resolved[i]));
        }
        st8(descriptor + 8 + length, 0);  // terminate what was written, not what was asked for
    }
    misc().pending_resource_name = resolved;
}

// Settings #0 get(name, value, size): copies the setting's value to *value (capacity *size) and
// writes the length back to *size. This game asks for "Language" and "TimeFormat", once each at
// boot.  (Stub::SettingGet)
uint32_t setting(GuestAddress name, GuestAddress value, GuestAddress size) {
    log_call("Settings", 0, {name, value, size});
    if (value == 0) {
        return SETTING_BAD_ARGUMENT;
    }
    const std::string key = read_guest_string(name, 32);
    const uint32_t capacity = size != 0 ? ld32(size) : 4;
    uint32_t written = 0;
    if (key == "Language") {
        if (capacity >= 4) {
            st32(value, misc().language);
        }
        written = 4;
    } else if (key == "TimeFormat") {
        const char twelve_hour[] = "12";  // the emulator's default
        for (uint32_t i = 0; i < sizeof twelve_hour && i < capacity; ++i) {
            st8(value + i, static_cast<uint8_t>(twelve_hour[i]));
        }
        written = std::min<uint32_t>(sizeof twelve_hour, capacity);
    }
    if (written == 0) {
        return SETTING_UNKNOWN;
    }
    if (size != 0) {
        st32(size, written);
    }
    return 0;
}

}  // namespace cubis::device
