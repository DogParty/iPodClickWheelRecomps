// Sound effects and the audio device: the five slots of loaded sounds (SoundSlots, a handle per
// index), playing and releasing them, the music and brightness levels the options pages
// adjust with the wheel, the battery reading, and the thin wrappers over the Audio framework
// that the rest of the game names.
#include "sounds.h"

#include "calling.h"
#include "framework/audio.h"
#include "framework/device.h"
#include "game_state.h"
#include "libc.h"
#include "records.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "state.h"

namespace minigolf::game {

namespace {

constexpr uint32_t SLOT_INDEX_LIMIT = 0x40;
constexpr uint32_t NO_SOUND = 0xffff'ffffu;
constexpr uint32_t LEVEL_MAX = 100;
// The audio device's block (0x1801bdd4): the battery lock (taken once, never released), the
// time of the last battery read, and the level it read (0..20).
DeviceBlock& device() {
    return device_block();
}
// What battery_status reports: the percentage and two flags, low and critical.
struct [[gnu::packed]] BatteryStatus {
    uint32_t level, low, critical, spare;
};
BatteryStatus& as_battery(uint32_t address) {
    return guest<BatteryStatus>(address);
}
constexpr uint32_t BATTERY_INTERVAL = 0xe4'e1c0, BATTERY_LEVELS = 20;

uint32_t& slot_handle(uint32_t slot, uint32_t index) {
    return sound_slots().handle[slot][index];
}
bool slot_valid(uint32_t slot, uint32_t index) {
    return slot < SLOT_COUNT && static_cast<int32_t>(index) >= 0 &&
           static_cast<int32_t>(index) < static_cast<int32_t>(SLOT_INDEX_LIMIT) &&
           sound_slots().enabled[slot] != 0;
}

// A level nudged by the wheel: the slot's step halved, following the last step by one at a
// time so a fast spin ramps rather than jumps.
int32_t wheel_step(uint32_t slot_step, uint32_t last_step) {
    const int32_t half = static_cast<int32_t>(slot_step) / 2;
    const int32_t last = static_cast<int32_t>(last_step);
    return half > last ? last + 1 : half < last ? last - 1 : last;
}

}  // namespace

SoundSlots& sound_slots() {
    static SoundSlots slots;
    return slots;
}

// 0x1800ddc8 — play a loaded sound: volume, pan and rate set, then started. The start is a
// tail call: it returns to the caller.
void sound_effect_play(uint32_t slot, uint32_t index, uint32_t volume, uint32_t rate,
                       uint32_t pan) {
    uint32_t handle = NO_SOUND;
    {
        if (!slot_valid(slot, index)) {
            return;
        }
        handle = slot_handle(slot, index);
        audio::set_sound_volume(handle, volume);
        audio::set_sound_pan(handle, pan);
        audio::set_sound_rate(handle, rate);
    }
    audio::play_sound(handle);
}

// 0x1800de40 — stop a loaded sound (a tail call).
void sound_slot_stop(uint32_t slot, uint32_t index) {
    if (!slot_valid(slot, index)) {
        return;
    }
    const uint32_t handle = slot_handle(slot, index);
    if (static_cast<int32_t>(handle) >= 0) {
        audio::stop_sound(handle);
    }
}

// 0x180121f4 — a fresh sound from the framework for a slot entry; its handle, or -1 when
// the slot or index is out of range or the slot is disabled.
uint32_t sound_slot_create(uint32_t slot, uint32_t index) {
    if (!slot_valid(slot, index)) {
        return NO_SOUND;
    }
    // The framework names the file by the index; the slot's flag is what r0 happens to hold.
    slot_handle(slot, index) = audio::create_sound(sound_slots().enabled[slot], index);
    return slot_handle(slot, index);
}

// 0x1801224c — whether a slot entry holds a sound (a handle that is not negative).
uint32_t sound_slot_present(uint32_t slot, uint32_t index) {
    return static_cast<int32_t>(slot_handle(slot, index)) < 0 ? 0 : 1;
}

// 0x1801051c — let a loaded sound go: its data freed, its handle released, the slot emptied.
void sound_slot_release(uint32_t slot, uint32_t index) {
    if (!slot_valid(slot, index)) {
        return;
    }
    const uint32_t handle = slot_handle(slot, index);
    if (static_cast<int32_t>(handle) < 0) {
        return;
    }
    const uint32_t data = audio::sound_data(handle);
    if (data != 0) {
        libc::heap_free(data);
    }
    audio::release_sound(slot_handle(slot, index));
    slot_handle(slot, index) = NO_SOUND;
}

// 0x1800fc74 — every slot emptied.
uint32_t sound_slots_release_all() {
    for (uint32_t slot = 0; slot < SLOT_COUNT; ++slot) {
        sound_slots().enabled[slot] = static_cast<uint8_t>(0);
        for (uint32_t index = 0; index < SLOT_INDEX_LIMIT; ++index) {
            sound_slot_release(slot, index);
        }
    }
    return 0;
}

// 0x18013fdc — the music level follows the wheel on the volume page (event 0 down, 1 up;
// anything else resets the ramp), then the device is told; with no change, the device's own
// level is read back into the setting.
//
// The slot each direction reads looks crossed, and is not: event 0 takes its step from slot 1
// and event 1 from slot 0, which is what the original does (0x18013ffc reads the slot table's
// +4 and 0x1801401c its +0xc; `WheelSlot` is flags, count, step, so those are slots 0 and 1 —
// and the branches are the other way round from the selection). `brightness_adjust` below is
// the same function for the screen and reads them the same way (0x18014c24, 0x18014c44).
//
// KNOWN BUG, not here: in this port neither slider actually moves, because the slot the wheel
// fills is the other one from the slot the original fills. The pure recompilation, running the
// original instructions, walks the level from 0 to 40 over forty frames on the same scripted
// input where this walks it nowhere. The difference is upstream in `input_gather`
// (input.cpp, 0x180082c4), which decides *which* slot a direction goes to, and correcting it
// changes what every screen reads from the wheel — so it wants a change of its own, with the
// second oracle to measure it. Swapping the slots here instead was tried and is wrong: it moves
// the level, but by a different ramp than the original, and `recomp_pages` says so.
void music_level_adjust() {
    uint32_t& level =
        guest<uint32_t>(PLAY + static_cast<uint32_t>(offsetof(PlayState, music_level)));
    const int32_t before = static_cast<int32_t>(level);
    const uint32_t event = static_cast<uint32_t>(text_block().selection);
    if (event == 0) {
        const int32_t step = wheel_step(wheel_slot_at(1).step, options_scratch().wheel_step_last);
        level = static_cast<uint32_t>(before - step < 0 ? 0 : before - step);
        options_scratch().wheel_step_last = static_cast<uint32_t>(step);
    } else if (event == 1) {
        const int32_t step = wheel_step(wheel_slot_at(0).step, options_scratch().wheel_step_last);
        level = static_cast<uint32_t>(before + step > static_cast<int32_t>(LEVEL_MAX)
                                          ? static_cast<int32_t>(LEVEL_MAX)
                                          : before + step);
        options_scratch().wheel_step_last = static_cast<uint32_t>(step);
    } else {
        options_scratch().wheel_step_last = 0;
    }
    if (static_cast<int32_t>(level) !=
        before) {  // the device takes the level as a fraction of its own
        const uint32_t scale = audio::music_level_scale();
        audio::set_music_level(libc::unsigned_divide(level * scale, LEVEL_MAX).quotient);
        return;
    }
    const uint32_t scale =
        audio::music_level_scale();  // unchanged: read the device's own level back
    const uint32_t device_level = audio::music_level();
    level = libc::unsigned_divide(LEVEL_MAX * device_level, scale).quotient;
}

// 0x18014c08 — the same for the screen's brightness.
uint32_t brightness_adjust() {
    uint32_t& level =
        guest<uint32_t>(PLAY + static_cast<uint32_t>(offsetof(PlayState, device_level)));
    const int32_t before = static_cast<int32_t>(level);
    const uint32_t event = static_cast<uint32_t>(text_block().selection);
    if (event == 0) {
        const int32_t step =
            wheel_step(wheel_slot_at(1).step, options_scratch().brightness_step_last);
        level = static_cast<uint32_t>(before - step < 0 ? 0 : before - step);
        options_scratch().brightness_step_last = static_cast<uint32_t>(step);
    } else if (event == 1) {
        const int32_t step =
            wheel_step(wheel_slot_at(0).step, options_scratch().brightness_step_last);
        level = static_cast<uint32_t>(before + step > static_cast<int32_t>(LEVEL_MAX)
                                          ? static_cast<int32_t>(LEVEL_MAX)
                                          : before + step);
        options_scratch().brightness_step_last = static_cast<uint32_t>(step);
    } else {
        options_scratch().brightness_step_last = 0;
    }
    if (static_cast<int32_t>(level) != before) {
        device::set_brightness(level > LEVEL_MAX - 1 ? LEVEL_MAX : level);
        return 1;
    }
    level = device::brightness();
    return 1;
}

// 0x180140cc — the battery: read from the device at most every quarter hour (under a lock the
// first time), as a percentage, with two flags — low below a third, critical at a quarter.
uint32_t battery_status(BatteryStatus& status) {
    GuestScratch frame(4 * 6);  // r3's slot holds the time read
    if ((device().lock & 1) == 0) {
        if (device().lock == 0) {
            device().lock = 1;
            device().battery_level = device::battery_level();
        }
    }
    const uint32_t now_at = frame.at(4 * 0);  // the clock is read into a stack slot
    guest<uint32_t>(now_at) = 0;
    (void)device::clock_microseconds(now_at);  // read back from the slot
    if (guest<uint32_t>(now_at) - device().battery_time > BATTERY_INTERVAL) {
        device().battery_level = device::battery_level();
        device().battery_time = guest<uint32_t>(now_at);
    }
    if (device().battery_level > BATTERY_LEVELS) {
        device().battery_level = BATTERY_LEVELS;
    }
    const uint32_t level = device().battery_level;
    const libc::Division percent = libc::unsigned_divide(level * LEVEL_MAX, BATTERY_LEVELS);
    status.level = percent.quotient;
    status.spare = 0;
    if (level > 12) {
        status.low = 0;
        status.critical = 0xffff;
    } else {
        status.critical = level <= 6 ? 0 : 0xffff;
        status.low = 0xffff;
    }
    return 1;
}

// 0x180141a8 — the battery percentage, with the flags into whichever of the three the caller
// gave somewhere to put them.
uint32_t battery_query(uint32_t low_out, uint32_t critical_out, uint32_t spare_out) {
    GuestScratch frame(4 * 8);  // the argument slots hold the status
    battery_status(as_battery(frame.at(4 * 0)));
    const BatteryStatus& status = as_battery(frame.at(4 * 0));
    if (low_out != 0) {
        guest<uint32_t>(low_out) = status.low;
    }
    if (critical_out != 0) {
        guest<uint32_t>(critical_out) = status.critical;
    }
    if (spare_out != 0) {
        guest<uint32_t>(spare_out) = status.spare;
    }
    return status.level;
}

}  // namespace minigolf::game
