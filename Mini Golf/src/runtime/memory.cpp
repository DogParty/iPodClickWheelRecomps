// Guest address space: one zero-filled reservation, bounds-checked access. See memory.h for the
// layout, and `reserve_span` below for how the reservation is made on each kind of host.
#include "runtime/memory.h"

#include "runtime/runtime.h"

#include <cstdio>
#include <cstdlib>

// A host with demand paging maps the whole 790 MB span in one go and lets the pages arrive as
// they are touched. A console has neither the address space to spare nor the paging to make it
// cheap, so it gets the four regions the guest actually uses, each allocated outright.
//
// -DMINIGOLF_REGION_MEMORY builds the console's model on a desktop, which is the only way to run
// the oracle against it: the tests cannot be run on the console itself.
#if defined(MINIGOLF_REGION_MEMORY)
// The regions, chosen deliberately.
#elif defined(_WIN32)
#define MINIGOLF_FLAT_GUEST_MEMORY 1
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#define MINIGOLF_FLAT_GUEST_MEMORY 1
#include <sys/mman.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#define MINIGOLF_HAVE_BACKTRACE 1
#include <execinfo.h>
#endif
#endif

namespace ipod {

namespace {

#if defined(MINIGOLF_FLAT_GUEST_MEMORY)
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

#if defined(MINIGOLF_FLAT_GUEST_MEMORY)
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

void watch_hit(uint32_t address, uint32_t value) {
    std::fprintf(stderr, "watch: store of %08x at %08x\n", value, address);
#if defined(MINIGOLF_HAVE_BACKTRACE)
    void* frames[24];
    const int count = backtrace(frames, 24);
    backtrace_symbols_fd(frames, count, 2);
#else
    std::fprintf(stderr, "watch: no backtrace on this platform; run under a debugger\n");
#endif
    std::abort();
}

void guest_memory_init() {
    if (const char* watch = std::getenv("MINIGOLF_WATCH")) {
        watch_address = static_cast<uint32_t>(std::strtoul(watch, nullptr, 16));
    }
#if defined(MINIGOLF_FLAT_GUEST_MEMORY)
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

uint8_t* guest_pointer(uint32_t address, uint32_t length) {
#if defined(MINIGOLF_FLAT_GUEST_MEMORY)
    // One unsigned comparison covers both "below GUEST_LOW" (wraps to a huge offset) and "past
    // GUEST_HIGH". A length past the whole span is checked first, because `GUEST_SPAN - length`
    // would wrap and let everything through — callers do pass sizes from the image.
    const uint32_t offset = address - GUEST_LOW;
    if (length > GUEST_SPAN || offset > GUEST_SPAN - length) {
        fatal("guest access outside mapped memory: %#010x (%u bytes)", address, length);
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
    fatal("guest access outside mapped memory: %#010x (%u bytes)", address, length);
#endif
}

uint32_t guest_address(const void* pointer) {
    const auto* at = static_cast<const uint8_t*>(pointer);
#if defined(MINIGOLF_FLAT_GUEST_MEMORY)
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
    // Say which regions were looked in. On a console this message is all there is — no debugger,
    // no core file — so it carries the map: whether the regions exist at all separates "asked too
    // early" from "a pointer that was never guest memory", and the two want different fixes.
    char map[256] = {};
    size_t written = 0;
    for (const Region& region : regions) {
        written += static_cast<size_t>(
            std::snprintf(map + written, sizeof map - written, " [%08x %p+%uk]", region.base,
                          static_cast<const void*>(region.memory), region.size / 1024));
        if (written >= sizeof map) {
            break;
        }
    }
    // Where the question came from, as an offset from this function: a console has no backtrace,
    // and the offset can be resolved against the .elf the .nro was built from (README).
    const auto here =
        reinterpret_cast<const uint8_t*>(reinterpret_cast<const void*>(&guest_address));
    const auto* caller = static_cast<const uint8_t*>(__builtin_return_address(0));
    fatal("host pointer %p is not in guest memory; asked from guest_address%+ld; regions:%s",
          pointer, static_cast<long>(caller - here), map);
#endif
}

}  // namespace ipod
