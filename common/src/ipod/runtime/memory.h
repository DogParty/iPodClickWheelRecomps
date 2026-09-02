// The guest address space: one flat reservation covering everything the game can address.
//
// The layout mirrors the emulator's (reference/eapp-loader/play.rs) so guest addresses in the
// recomp's logs line up with the emulator's logs byte for byte:
//
//   0x11000000  RAM         8 MB   stack lives at the top
//   0x18000000  image       eApp file contents, then the BSS span the game zero-fills
//   0x19000000  heap       64 MB   served by miscTBD alloc/free (src/libeapp)
//   0x40000000  IRAM      128 KB   the iPod's on-chip SRAM; the game keeps a few tables there
//
// The whole range 0x11000000..0x40020000 is reserved up front, zero-filled and committed only
// as it is touched (memory.cpp `reserve_span` does this three ways, one per kind of host).
// Every access is bounds-checked: an out-of-range guest address is a bug in the recomp or in
// the game, and either deserves a clear fatal error instead of a host crash.
//
// The `ld`/`st` accessors below assemble bytes explicitly rather than type-punning, so they are
// correct whatever the host's byte order and alignment rules, and still compile to a single
// load. The game's structure overlays (src/game/guest.h) do not: they reinterpret guest bytes
// as a packed struct, which requires a little-endian host. That is the project's one endianness
// assumption and guest.h asserts it.
#pragma once

#include <cstdint>

namespace ipod {

constexpr uint32_t RAM_BASE = 0x1100'0000u;
constexpr uint32_t RAM_SIZE = 8u * 1024u * 1024u;
constexpr uint32_t IMAGE_BASE = 0x1800'0000u;
constexpr uint32_t IMAGE_SPAN = 8u * 1024u * 1024u;  // file contents + BSS, as the emulator maps it
constexpr uint32_t HEAP_BASE = 0x1900'0000u;
constexpr uint32_t HEAP_SIZE = 64u * 1024u * 1024u;
constexpr uint32_t IRAM_BASE = 0x4000'0000u;
constexpr uint32_t IRAM_SIZE = 128u * 1024u;

constexpr uint32_t GUEST_LOW = RAM_BASE;
constexpr uint32_t GUEST_HIGH = IRAM_BASE + IRAM_SIZE;
constexpr uint32_t GUEST_SPAN = GUEST_HIGH - GUEST_LOW;

// The four places the guest keeps anything, and how much of each is actually backed on a target
// with no demand paging (a console; see memory.cpp). The address space between them is empty —
// 790 MB of nothing between the image and the IRAM — which a host with demand paging can map in
// one go and a console cannot. The sizes below are what the game was measured to touch, with
// room: the longest recorded session reaches 0x180e1b24 in the image and 0x1940c5cd in the heap.
// Going past one of them is a bounds error from `guest_pointer`, not silent corruption.
constexpr uint32_t IMAGE_BACKED = 4u * 1024u * 1024u;
constexpr uint32_t HEAP_BACKED = 16u * 1024u * 1024u;

// Reserve the guest address space. Must be called once before any access; fatal on failure.
void guest_memory_init();

// IPOD_WATCH=ADDR (hex) in the environment: every non-zero store that touches that word
// stops the process with SIGTRAP, so a debugger shows who wrote it. 0 when unset.
extern uint32_t watch_address;
void watch_hit(uint32_t address, uint32_t value);
inline void watch_store(uint32_t address, uint32_t length, uint32_t value) {
    if (watch_address != 0 && value != 0 && address < watch_address + 4 &&
        watch_address < address + length) {
        watch_hit(address, value);
    }
}

// Host pointer for a guest address range [address, address + length). Fatal when out of range.
uint8_t* guest_pointer(uint32_t address, uint32_t length);

// The guest address a host pointer into guest memory names — the inverse of `guest_pointer`.
// Fatal when the pointer is not in guest memory.
uint32_t guest_address(const void* pointer);

inline uint32_t ld32(uint32_t address) {
    const uint8_t* p = guest_pointer(address, 4);
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint32_t ld16(uint32_t address) {
    const uint8_t* p = guest_pointer(address, 2);
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
}

inline uint32_t ld8(uint32_t address) {
    return *guest_pointer(address, 1);
}

// Sign-extending loads (LDRSH / LDRSB) return the 32-bit register value the ARM would hold.
inline uint32_t ld16s(uint32_t address) {
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(ld16(address))));
}

inline uint32_t ld8s(uint32_t address) {
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(ld8(address))));
}

inline void st32(uint32_t address, uint32_t value) {
    watch_store(address, 4, value);
    uint8_t* p = guest_pointer(address, 4);
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

inline void st16(uint32_t address, uint32_t value) {
    watch_store(address, 2, value);
    uint8_t* p = guest_pointer(address, 2);
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

inline void st8(uint32_t address, uint32_t value) {
    watch_store(address, 1, value);
    *guest_pointer(address, 1) = static_cast<uint8_t>(value);
}

}  // namespace ipod
