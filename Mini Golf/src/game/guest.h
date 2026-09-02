// Typed views of the game's state in guest memory. The game keeps its state where the original
// kept it — the frameworks are handed pointers into it and the call log compares them — so a
// structure here is an overlay: the same bytes, with the original layout pinned by
// static_asserts, read and written as fields instead of offsets.
//
// This is the one place in the project that assumes anything about the host: an overlay reads
// the guest's little-endian words as the host's own, so the host must be little-endian too.
// Everything else goes through the byte-at-a-time accessors in runtime/memory.h, which do not
// care. Porting to a big-endian host means giving each field an accessor that swaps, not
// changing anything else.
#pragma once

#include "runtime/memory.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace minigolf::game {

// The ARM the game ran on was little-endian, and an overlay reads its words directly. C++17 has
// no portable way to ask, so this checks where the compiler says (GCC, Clang) and stays quiet
// where it does not — MSVC targets no big-endian machine.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "the guest-memory overlays require a little-endian host");
#endif

// The structure that lives at a guest address. Guest memory is one host allocation, so the
// reference is direct; the structures are packed because the game aligned nothing beyond
// its fields' natural sizes and a few words sit at odd offsets.
template <class T>
T& guest(uint32_t address) {
    static_assert(std::is_trivially_copyable_v<T>);
    return *reinterpret_cast<T*>(guest_pointer(address, sizeof(T)));
}

// Where a structure the game passed us lives. The bridge back to an address, for the fields and
// framework arguments that still hold one; it disappears with the last of them.
//
// The reference is not const, and that is the point. Guest structures are packed, and a *const*
// reference will happily bind to a field of one by copying it — so `address_of(record.field)`
// compiles, and hands back the address of a copy on the host stack that was never guest memory.
// That is a silent wrong answer, and it cost an evening on the Switch, where the copy is what
// aarch64-none-elf-g++ does. A non-const reference cannot bind to a packed field at all: the same
// mistake is now a compiler error there. Use `field_address` for a field.
template <class T>
uint32_t address_of(T& object) {
    return guest_address(&object);
}

// The address of a field of a guest structure: the structure's own address plus the offset, which
// is always right and never involves a reference to the field itself.
//
//     field_address(context, offsetof(ContextBlock, clock))
template <class T>
uint32_t field_address(T& object, size_t offset) {
    return address_of(object) + static_cast<uint32_t>(offset);
}

// An array of T at a guest address (a table in the image, or a buffer the game keeps).
template <class T>
T* guest_array(uint32_t address) {
    return &guest<T>(address);
}

}  // namespace minigolf::game
