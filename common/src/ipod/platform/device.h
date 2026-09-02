// What the host machine can say about itself that the iPod's firmware said about the device:
// the wall clock and the battery.
//
// **Why this is shared.** Every title asks for both through `miscTBD` — #12 for the clock, #13
// for the charge — and every title had its own copy of the answer, which is how five copies came
// to disagree. The Cubis 2 recomp found the clock wrong in all of them (2026-08-28): the hour was
// handed over already converted to 12-hour, and the games do that conversion themselves from a
// 24-hour value, so every title drew its status bar with the afternoon folded onto the morning.
// The battery was `return 100;` in all five. Neither is a fact about a *binary*, which is the
// test for what belongs here (../../../README.md): they are facts about the machine the port is
// running on, and there is one right answer per platform.
//
// **Why it is platform-specific.** There is no portable way to ask for a battery, and the three
// platforms that matter each have one. `device.cpp` has an implementation per platform and a
// fallback that says "full" for a machine with no battery to report — a desktop, or a platform
// nobody has written the call for yet.
#pragma once

namespace ipod::platform {

// The host's local time, broken down. `hour` is **0..23**: the games convert to 12-hour and
// choose AM or PM themselves (Cubis 2 at `0x1800de64`, which tests the hour against 12 and
// subtracts), so handing them a 12-hour value tells them it is always afternoon.
struct LocalTime {
    int second = 0;
    int minute = 0;
    int hour = 0;    // 0..23
    int day = 1;     // 1..31
    int month = 1;   // 1..12
    int year = 1970; // full year
};

[[nodiscard]] LocalTime local_time_now();

// Pin the time of day that `local_time_now` reports, so a replay draws the same digits whenever
// it is run. The date stays the host's and the seconds are zeroed. `hour` outside 0..23 clears
// it and puts the host's clock back.
void set_fixed_local_time(int hour, int minute);

// How charged the host is, 0..100. A machine with no battery — or a platform with no
// implementation here — reports 100, which is what a device on the charger reports too, so
// nothing downstream has to know the difference.
[[nodiscard]] unsigned battery_percent();

// Pin the charge, for a run that has to be reproducible. Outside 0..100 puts the host's back.
void set_fixed_battery_percent(int percent);

}  // namespace ipod::platform
