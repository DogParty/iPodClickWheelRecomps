// Services the generated code calls that are neither CPU arithmetic nor memory access.
//
// These are the hooks for the few ARM idioms that have no direct C++ equivalent: the game's own
// assert traps, the semihosting calls armcc's library makes, and fatal errors. Implemented in
// src/runtime/runtime.cpp.
#pragma once

#include "ipod/runtime/fatal.h"
#include "ipod/runtime/cpu.h"

#include <cstdint>
#include <initializer_list>

namespace ipod {

// Dying with something said first is the shared core's (`ipod/runtime/fatal.h`): it is the
// one runtime service that takes no `Cpu&`. Everything below does, and stays here.

// The original code's `b .` — an infinite loop armcc emitted for failed assertions. The game has
// 354 of these; hitting one means a game invariant was violated, which in practice means a bug in
// the recomp or in a framework implementation. Reports the address and exits.
[[noreturn]] void assert_trap(uint32_t address);

// `svc 0x123456`: ARM semihosting, used by the C library for `exit()` and debug output. Logged;
// the operation number is in r0 and its parameter block in r1.
void semihost(Cpu& cpu, uint32_t number);

// Debug aid: every recompiled function reports its entry here. Off unless `--trace-entry=ADDR`
// selected addresses, in which case arrivals print the registers — the same view the emulator's
// `play --watch-pc` gives, so the two can be compared instruction-precisely at a chosen point.
void trace_entry_watch(uint32_t address);  // add an address to watch
// `--dump-entry=ADDR:START:BYTES`: at every entry to ADDR, also print BYTES of guest memory
// from START as words — for comparing a table two builds fill differently.
void trace_entry_dump(uint32_t address, uint32_t start, uint32_t bytes);
void trace_entry_report(uint32_t address);

inline bool& trace_entries_enabled() {
    static bool enabled = false;
    return enabled;
}

inline void trace_entry(uint32_t address) {
    if (trace_entries_enabled()) {
        trace_entry_report(address);
    }
}

}  // namespace ipod
