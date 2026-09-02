// The miscTBD and Settings frameworks: memory, clock, device level, resource names, settings.
//
// "miscTBD" is the name the reverse-engineering gave the grab-bag framework that every title
// imports: the heap, the microsecond clock, the backlight/volume level, the wall clock, the
// battery, and the resource-name resolver. Each ordinal's behaviour is the emulator's
// (reference/eapp-loader/lib.rs, the Stub variant named in each comment).
#include "framework/device.h"
#include "heap.h"
#include "host_state.h"
#include "ipod_eapp.h"
#include "ipod/platform/device.h"
#include "runtime/memory.h"

#include <algorithm>
#include <ctime>
#include <string>

namespace bowling::eapp {

namespace {

// The game's timebase: miscTBD #9 advances this much per call, making 60 Hz the speed at which
// the game's own timers agree with the frame rate. Fixed rather than wall time so a scripted
// run is reproducible (the emulator's `--fixed-clock`).
constexpr uint32_t CLOCK_STEP_MICROSECONDS = 16'667;

// Settings #0 error codes, as the firmware's settings dispatcher reports them.
constexpr uint32_t SETTING_BAD_ARGUMENT = static_cast<uint32_t>(-49);
constexpr uint32_t SETTING_UNKNOWN = static_cast<uint32_t>(-50);

constexpr uint32_t RESOLVED_NAME_CAPACITY = 80;

struct MiscState {
    uint32_t clock_microseconds = 0;
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

// Pin the wall clock, so a replay draws the same clock digits whenever it is run.
void set_fixed_host_time(int hour, int minute) {
    ipod::platform::set_fixed_local_time(hour, minute);
}

// Answer #12 and #13 as the emulator's stubs did, for the recordings in tests/expected/.
void set_emulator_device(bool emulator) {
    misc().emulator_device = emulator;
}

}  // namespace bowling::eapp

namespace bowling::device {

// The implementation lives in the file above.
using namespace bowling::eapp;  // NOLINT(google-build-using-namespace): one file, by design

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

// #9 clock(out) -> microseconds; also stored at *out.  (Stub::Clock, fixed step)
uint32_t clock_microseconds(GuestAddress out) {
    log_call("miscTBD", 9, {out});
    misc().clock_microseconds += CLOCK_STEP_MICROSECONDS;
    st32(out, misc().clock_microseconds);
    return misc().clock_microseconds;
}

// #12 wall clock into six words at *out: second, minute, hour (12-hour), day, month, year.
// Real local time, as the emulator reports it, unless a scripted run pinned it. (Stub::HostTime)
uint32_t wall_clock(GuestAddress out) {
    log_call("miscTBD", 12, {out});
    if (out == 0) {
        return 0;
    }
    const ipod::platform::LocalTime now = ipod::platform::local_time_now();
    const int hour =
        misc().emulator_device ? (now.hour % 12 == 0 ? 12 : now.hour % 12) : now.hour;
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

// #13 battery level in fifths (0..20), from a percentage. The emulator reads the host's
// battery; a fixed full charge keeps scripted runs reproducible.  (Stub::HostBattery)
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
// writes the length back to *size. "Language" is the only one Lost asks for.
// (Stub::SettingGet)
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

}  // namespace bowling::device
