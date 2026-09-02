// What is left of the ARM calling conventions. The game's own functions are plain C++ now, and
// so is every call into the platform (src/framework/); two things still follow the original ABI:
//   * the entries in the dispatch table (the image's vectors and the completions the file
//     framework dispatches by address) are called with their arguments in r0–r3 (`call_entry`),
//     and read any more from the stack (`stack_argument`);
//   * locals a framework reads or writes live on the guest stack (`GuestScratch`), where the
//     original kept them, so the addresses the game passes around stay real guest addresses.
#pragma once

#include "runtime/cpu.h"
#include "runtime/memory.h"

#include <cstdint>
#include <initializer_list>

namespace minigolf::game {

// Call a recompiled function or a framework entry as the original code did: arguments in
// r0–r3 and lr set to the original return address (the `bl` site + 4). Arguments beyond four
// Write the words a callee reads from [sp] onwards — its fifth argument and beyond. Only the
// dispatch-table entries still take arguments this way.
inline void stack_arguments(std::initializer_list<uint32_t> words) {
    uint32_t at = registers().r[SP];
    for (const uint32_t word : words) {
        st32(at, word);
        at += 4;
    }
}

// Bytes reserved on the guest stack for the locals a framework reads or writes — an
// out-parameter, a string built for the text renderer, a request record. The original kept
// these among the registers it pushed; only the space matters now. `at(offset)` is the guest
// address of a local, `offset` bytes above the lowest reserved address.
class GuestScratch {
public:
    explicit GuestScratch(uint32_t bytes) : bytes_(bytes) {
        registers().r[SP] -= bytes;
        base_ = registers().r[SP];
    }
    ~GuestScratch() { registers().r[SP] += bytes_; }
    GuestScratch(const GuestScratch&) = delete;
    GuestScratch& operator=(const GuestScratch&) = delete;

    [[nodiscard]] uint32_t at(uint32_t offset) const { return base_ + offset; }

private:
    uint32_t bytes_;
    uint32_t base_ = 0;
};

// Run one of the game's own ARM-ABI entries (a dispatch-table target): arguments in r0-r3.
inline uint32_t call_entry(void (*entry)(Cpu&), std::initializer_list<uint32_t> arguments) {
    Cpu& cpu = registers();
    unsigned index = 0;
    for (const uint32_t argument : arguments) {
        cpu.r[index++] = argument;
    }
    entry(cpu);
    return cpu.r[0];
}

// The i-th word the caller pushed (an ARM-ABI entry's fifth argument and on).
inline uint32_t stack_argument(uint32_t index) {
    return ld32(registers().r[SP] + index * 4);
}
}  // namespace minigolf::game
