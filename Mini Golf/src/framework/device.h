// The device the game runs on: its memory, its clock, its battery, its backlight, its settings.
//
// This is the iPod's "miscTBD" framework — the grab-bag every title imports — plus the settings
// dispatcher. `src/libeapp/misc.cpp` implements it.
#pragma once

#include "framework/types.h"

#include <cstdint>

namespace minigolf::device {

// --- memory --------------------------------------------------------------------------------

// The device heap the game allocates from. Answers 0 when it is exhausted.
[[nodiscard]] GuestAddress allocate(uint32_t bytes);
void release(GuestAddress memory);

// --- time ----------------------------------------------------------------------------------

// Microseconds since start-up, also stored at `out`. The game's whole timebase.
[[nodiscard]] uint32_t clock_microseconds(GuestAddress out);

// The wall clock into six words at `out`: second, minute, hour (12-hour), day, month, year.
[[nodiscard]] uint32_t wall_clock(GuestAddress out);

// The rate the game divides clock readings by, and a word it reads beside it. The hardware
// answered 1000 and 0; both are read together wherever the game converts a duration.
[[nodiscard]] uint32_t clock_rate();
[[nodiscard]] uint32_t clock_reserved();

// --- power and screen ----------------------------------------------------------------------

// The battery in fifths of a charge, 0..20.
[[nodiscard]] uint32_t battery_level();

// The backlight level, 0..100.
[[nodiscard]] uint32_t brightness();
void set_brightness(uint32_t level);

// Tell the device the player is active (1) or that the idle timer may run again (0).
void set_idle_inhibited(uint32_t inhibited);

// --- names and settings ----------------------------------------------------------------------

// Resolve a resource name into the descriptor at `descriptor`: two header words and the name.
// The name is remembered for the next stream the game registers.
void resolve_resource(uint32_t reserved, GuestAddress descriptor, uint32_t flags,
                      GuestAddress name);

// Read a setting by name into `value` (capacity at `size`, which receives the length written).
// Answers 0, or a negative firmware error code. "Language" is the only one Mini Golf asks for.
[[nodiscard]] uint32_t setting(GuestAddress name, GuestAddress value, GuestAddress size);

}  // namespace minigolf::device
