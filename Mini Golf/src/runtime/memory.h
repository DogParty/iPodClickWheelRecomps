// The guest address space — the shared core's, brought into this title's namespace.
//
// The definitions are in `recomps/common/src/ipod/runtime/memory.h`, which was identical
// in both trees: the device is the same device. `memory.cpp` is still this title's,
// because how much of each region is actually backed is measured per game, and it now
// defines into `ipod`.
//
// See ../../../common/README.md.
#pragma once

#include "ipod/runtime/memory.h"

namespace minigolf {
using ::ipod::RAM_BASE;
using ::ipod::RAM_SIZE;
using ::ipod::IMAGE_BASE;
using ::ipod::IMAGE_SPAN;
using ::ipod::HEAP_BASE;
using ::ipod::HEAP_SIZE;
using ::ipod::IRAM_BASE;
using ::ipod::IRAM_SIZE;
using ::ipod::GUEST_LOW;
using ::ipod::GUEST_HIGH;
using ::ipod::GUEST_SPAN;
using ::ipod::IMAGE_BACKED;
using ::ipod::HEAP_BACKED;
using ::ipod::guest_memory_init;
using ::ipod::guest_pointer;
using ::ipod::guest_address;
using ::ipod::watch_address;
using ::ipod::watch_hit;
using ::ipod::watch_store;
using ::ipod::ld32;
using ::ipod::ld16;
using ::ipod::ld8;
using ::ipod::ld16s;
using ::ipod::ld8s;
using ::ipod::st32;
using ::ipod::st16;
using ::ipod::st8;
}  // namespace minigolf
