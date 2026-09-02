// Runtime services for the recompiled code: fatal errors, assert traps, semihosting.
#include "ipod/runtime/runtime.h"

#include "ipod/runtime/memory.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace ipod {

namespace {

void print_cpu(const Cpu& cpu) {
    std::fprintf(stderr, "  r0-r3  %08x %08x %08x %08x\n", cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3]);
    std::fprintf(stderr, "  r4-r7  %08x %08x %08x %08x\n", cpu.r[4], cpu.r[5], cpu.r[6], cpu.r[7]);
    std::fprintf(stderr, "  r8-r11 %08x %08x %08x %08x\n", cpu.r[8], cpu.r[9], cpu.r[10],
                 cpu.r[11]);
    std::fprintf(stderr, "  r12 %08x  sp %08x  lr %08x  flags %c%c%c%c\n", cpu.r[12], cpu.r[SP],
                 cpu.r[LR], cpu.n ? 'N' : '-', cpu.z ? 'Z' : '-', cpu.c ? 'C' : '-',
                 cpu.v ? 'V' : '-');
}

}  // namespace

namespace {
}  // namespace



void assert_trap(uint32_t address) {
    const Cpu& cpu = registers();
    std::fflush(stdout);
    std::fprintf(stderr, "fatal: the game's assert trap at %#010x fired (lr %#010x)\n", address,
                 cpu.r[LR]);
    print_cpu(cpu);
    std::exit(EXIT_FAILURE);
}

// armcc's library uses semihosting for `exit` (operation 0x18, "ReportException") and debug
// I/O. The game itself never reaches these in normal play; log and carry on so a stray call
// shows up in the output instead of silently changing control flow.
void semihost(Cpu& cpu, uint32_t number) {
    std::fprintf(stderr, "semihosting call %#x: operation %#x parameter %#010x (lr %#010x)\n",
                 number, cpu.r[0], cpu.r[1], cpu.r[LR]);
    cpu.r[0] = 0;
}

namespace {

std::vector<uint32_t>& watched_entries() {
    static std::vector<uint32_t> watched;
    return watched;
}

struct EntryDump {
    uint32_t address, start, bytes;
};
std::vector<EntryDump>& entry_dumps() {
    static std::vector<EntryDump> dumps;
    return dumps;
}

}  // namespace

void trace_entry_watch(uint32_t address) {
    watched_entries().push_back(address);
    trace_entries_enabled() = true;
}

void trace_entry_dump(uint32_t address, uint32_t start, uint32_t bytes) {
    entry_dumps().push_back({address, start, bytes});
    trace_entries_enabled() = true;
}

void trace_entry_report(uint32_t address) {
    const Cpu& cpu = registers();
    for (const EntryDump& dump : entry_dumps()) {
        if (dump.address != address) {
            continue;
        }
        std::printf("dump %#010x at %#010x:", address, dump.start);
        for (uint32_t offset = 0; offset < dump.bytes; offset += 4) {
            std::printf("%s%08x", offset % 32 == 0 ? "\n  " : " ", ld32(dump.start + offset));
        }
        std::printf("\n");
    }
    for (const uint32_t watched : watched_entries()) {
        if (watched == address) {
            // Registers, then the first sixteen stack words: the arguments a callee with more
            // than four finds there.
            std::printf(
                "enter %#010x <-%#010x r0-7:%08x %08x %08x %08x %08x %08x %08x %08x sp %08x stack:",
                address, cpu.r[LR], cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3], cpu.r[4], cpu.r[5],
                cpu.r[6], cpu.r[7], cpu.r[SP]);
            for (uint32_t i = 0; i < 16; ++i) {
                std::printf("%s%08x", i == 0 ? "" : " ", ld32(cpu.r[SP] + 4 * i));
            }
            std::printf("\n");
        }
    }
}

}  // namespace ipod
