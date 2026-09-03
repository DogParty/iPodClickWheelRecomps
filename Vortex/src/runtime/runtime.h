// runtime.h — the shared core's, brought into this title's namespace.
//
// The definitions live in `common/src/ipod/runtime/runtime.h` and are compiled once for
// every title. This header exists so that nothing else in this tree has to know that: the include
// path and the qualified names callers already write are unchanged, and the names arrive by
// `using` rather than by alias, so this namespace can still hold what is genuinely this
// title's.
//
// See ../../../common/README.md.
#pragma once

#include "ipod/runtime/runtime.h"

#include <cstdint>
#include <initializer_list>

namespace vortex {
using ::ipod::assert_trap;
using ::ipod::fatal;
using ::ipod::semihost;
using ::ipod::set_fatal_handler;
using ::ipod::trace_entries_enabled;
using ::ipod::trace_entry;
using ::ipod::trace_entry_dump;
using ::ipod::trace_entry_report;
using ::ipod::trace_entry_watch;
}  // namespace vortex

namespace vortex::game {
// Call through a guest address: the vector-table entries and every `mov pc, rN` target.
// Fatal on an address that is not a function entry (src/game/dispatch.cpp).
void call_indirect(uint32_t target);

// The same for a target that takes arguments: they arrive in r0-r3 the way the original's
// `blx` left them, and the target's answer comes back in r0.
uint32_t call_indirect(uint32_t target, std::initializer_list<uint32_t> arguments);
}  // namespace vortex::game
