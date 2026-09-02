// Guest address space: one zero-filled reservation, bounds-checked access. See memory.h for the
// layout, and `reserve_span` below for how the reservation is made on each kind of host.
#include "runtime/memory.h"

#include "runtime/runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// A host with demand paging maps the whole 790 MB span in one go and lets the pages arrive as
// they are touched. A console has neither the address space to spare nor the paging to make it
// cheap, so it gets the four regions the guest actually uses, each allocated outright.
//
// -DBOWLING_REGION_MEMORY builds the console's model on a desktop, which is the only way to run
// the oracle against it: the tests cannot be run on the console itself.
#if defined(BOWLING_REGION_MEMORY)
// The regions, chosen deliberately.
#elif defined(_WIN32)
#define BOWLING_FLAT_GUEST_MEMORY 1
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#define BOWLING_FLAT_GUEST_MEMORY 1
#include <sys/mman.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#define BOWLING_HAVE_BACKTRACE 1
#include <execinfo.h>
#endif
#endif

namespace ipod {

namespace {

#if defined(BOWLING_FLAT_GUEST_MEMORY)
// The base of the reservation; guest address A lives at base + (A - GUEST_LOW). Set once by
// guest_memory_init; the accessor functions are the only readers.
uint8_t* reservation = nullptr;
#else
// One block per region, in the order they are searched: the heap is the busiest, then the image.
// Nothing the guest does spans two of them — they are megabytes apart — so an access can be
// resolved by finding its region and checking the offset within that one.
struct Region {
    uint32_t base;
    uint32_t size;
    uint8_t* memory;
};
Region regions[] = {
    {HEAP_BASE, HEAP_BACKED, nullptr},
    {IMAGE_BASE, IMAGE_BACKED, nullptr},
    {RAM_BASE, RAM_SIZE, nullptr},
    {IRAM_BASE, IRAM_SIZE, nullptr},
};
#endif

#if defined(BOWLING_FLAT_GUEST_MEMORY)
// Reserve GUEST_SPAN bytes, zero-filled, without committing what is never touched. The span is
// about 790 MB and the game touches a few megabytes of it, so lazy commitment matters.
uint8_t* reserve_span() {
#if defined(_WIN32)
    // Reserve and commit: Windows charges the commit limit but only faults pages in on first
    // touch, and the pages arrive zeroed.
    void* mapping = VirtualAlloc(nullptr, GUEST_SPAN, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return static_cast<uint8_t*>(mapping);
#else
    // MAP_NORESERVE: untouched pages are never committed. Anonymous mappings are zero-filled,
    // which is what the BSS and the heap expect.
    void* mapping = mmap(nullptr, GUEST_SPAN, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return mapping == MAP_FAILED ? nullptr : static_cast<uint8_t*>(mapping);
#endif
}
#endif

}  // namespace

uint32_t watch_address = 0;
// BOWLING_WATCH_VALUE narrows the watch to stores of one value, and the watch only fires while
// the entry trace is on (`--trace-entry=… --trace-from=N`), so a slot every function pushes into
// can be watched for the one write, in the one frame, that put the wrong thing there.
bool watch_value_given = false;
uint32_t watch_value = 0;

// BOWLING_WATCH_LOG=1 turns the watch into a log: every store is printed and the run goes on,
// which is what comparing a value's history against the emulator's `--watch-mem` needs.
bool watch_logs = false;

void watch_hit(uint32_t address, uint32_t value) {
    if (watch_logs) {
        std::fflush(stdout);
        std::fprintf(stderr, "watch: store of %08x at %08x\n", value, address);
        return;
    }
    if ((watch_value_given && value != watch_value) || !trace_entries_enabled()) {
        return;
    }
    std::fflush(stdout);
    std::fprintf(stderr, "watch: store of %08x at %08x\n", value, address);
#if defined(BOWLING_HAVE_BACKTRACE)
    void* frames[24];
    const int count = backtrace(frames, 24);
    backtrace_symbols_fd(frames, count, 2);
#else
    std::fprintf(stderr, "watch: no backtrace on this platform; run under a debugger\n");
#endif
    std::abort();
}

void guest_memory_init() {
    if (const char* watch = std::getenv("BOWLING_WATCH")) {
        watch_address = static_cast<uint32_t>(std::strtoul(watch, nullptr, 16));
    }
    watch_logs = std::getenv("BOWLING_WATCH_LOG") != nullptr;
    if (const char* value = std::getenv("BOWLING_WATCH_VALUE")) {
        watch_value_given = true;
        watch_value = static_cast<uint32_t>(std::strtoul(value, nullptr, 16));
    }
#if defined(BOWLING_FLAT_GUEST_MEMORY)
    if (reservation != nullptr) {
        return;
    }
    reservation = reserve_span();
    if (reservation == nullptr) {
        fatal("cannot reserve %u MB for the guest address space", GUEST_SPAN / (1024u * 1024u));
    }
#else
    for (Region& region : regions) {
        if (region.memory != nullptr) {
            continue;
        }
        region.memory = static_cast<uint8_t*>(std::calloc(region.size, 1));
        if (region.memory == nullptr) {
            fatal("cannot allocate %u MB for guest memory at %#010x", region.size / (1024u * 1024u),
                  region.base);
        }
    }
#endif
}

// An access to an address no region covers.
//
// The Sims Bowling reads a byte at 0x01400010 on its second frame — an address 250 MB below the
// lowest thing the firmware maps for an eApp — and the emulator, which records such an access
// as a finding and answers it with zero (reference/eapp-loader/lib.rs, `note_unmapped`), goes on
// to the main menu with it. So does the game on the device, presumably, where that address is
// whatever the PP5022 puts there. What the game wants from it is not established; until it is,
// this does what the emulator does, which is what the recordings contain: a read answers zero,
// a write goes nowhere, and either is counted and said once at exit. BOWLING_TRACE_UNMAPPED=1
// prints every one with a host backtrace, which names the recompiled function that made it.
// Anything wider than the scratch word is still the bug it always was.
constexpr uint32_t UNMAPPED_SCRATCH_BYTES = 16;
uint8_t unmapped_scratch[UNMAPPED_SCRATCH_BYTES];
unsigned unmapped_accesses = 0;
uint32_t first_unmapped_address = 0;

void report_unmapped_accesses() {
    std::fprintf(stderr,
                 "%u guest access(es) outside mapped memory, the first at %#010x — answered as "
                 "the emulator answers them (runtime/memory.cpp)\n",
                 unmapped_accesses, first_unmapped_address);
}

uint8_t* unmapped_access(uint32_t address, uint32_t length) {
    if (length > UNMAPPED_SCRATCH_BYTES) {
        fatal("guest access outside mapped memory: %#010x (%u bytes)", address, length);
    }
    if (unmapped_accesses++ == 0) {
        first_unmapped_address = address;
        std::atexit(report_unmapped_accesses);
    }
    static const bool trace = std::getenv("BOWLING_TRACE_UNMAPPED") != nullptr;
    if (trace) {
        std::fflush(stdout);
        std::fprintf(stderr, "unmapped: %#010x (%u bytes)\n", address, length);
#if defined(BOWLING_HAVE_BACKTRACE)
        void* frames[24];
        const int count = backtrace(frames, 24);
        backtrace_symbols_fd(frames, count, 2);
#endif
    }
    std::memset(unmapped_scratch, 0, sizeof unmapped_scratch);  // a read is always zero
    return unmapped_scratch;
}

uint8_t* guest_pointer(uint32_t address, uint32_t length) {
#if defined(BOWLING_FLAT_GUEST_MEMORY)
    // One unsigned comparison covers both "below GUEST_LOW" (wraps to a huge offset) and "past
    // GUEST_HIGH". A length past the whole span is checked first, because `GUEST_SPAN - length`
    // would wrap and let everything through — callers do pass sizes from the image.
    const uint32_t offset = address - GUEST_LOW;
    if (length > GUEST_SPAN || offset > GUEST_SPAN - length) {
        return unmapped_access(address, length);
    }
    return reservation + offset;
#else
    for (const Region& region : regions) {
        const uint32_t offset = address - region.base;
        if (offset < region.size) {
            if (length > region.size || offset > region.size - length) {
                fatal("guest access past the end of a region: %#010x (%u bytes)", address, length);
            }
            return region.memory + offset;
        }
    }
    return unmapped_access(address, length);
#endif
}

uint32_t guest_address(const void* pointer) {
    const auto* at = static_cast<const uint8_t*>(pointer);
#if defined(BOWLING_FLAT_GUEST_MEMORY)
    if (at < reservation || at >= reservation + GUEST_SPAN) {
        fatal("host pointer %p is not in guest memory", pointer);
    }
    return GUEST_LOW + static_cast<uint32_t>(at - reservation);
#else
    for (const Region& region : regions) {
        if (at >= region.memory && at < region.memory + region.size) {
            return region.base + static_cast<uint32_t>(at - region.memory);
        }
    }
    fatal("host pointer %p is not in guest memory", pointer);
#endif
}

}  // namespace ipod
