// call_indirect: the guest addresses the game stores as function pointers — the image's entry
// vectors, screen handlers/ticks/renders, file and sound completions, constructors — mapped
// to their ARM-ABI entries. Anything else is a bug: the original had no other indirect targets.
#include "runtime/memory.h"
#include "runtime/runtime.h"
#include "shims.h"

namespace minigolf::game {

uint32_t call_indirect(uint32_t target, std::initializer_list<uint32_t> arguments) {
    Cpu& cpu = registers();
    unsigned index = 0;
    for (const uint32_t argument : arguments) {
        cpu.r[index++] = argument;
    }
    call_indirect(target);
    return cpu.r[0];
}

void call_indirect(uint32_t target) {
    Cpu& cpu = registers();
    trace_entry(target);
    switch (target) {
    case 0x1800e644u:
        return f_1800e644(cpu);  // slot_flags_from_table
    case 0x18016ca0u:
        return f_18016ca0(cpu);  // file_object_completed (as_file_object(callback))
    case 0x18016e98u:
        return f_18016e98(cpu);  // slot completion thunk (open/continue)
    case 0x18016ec8u:
        return f_18016ec8(cpu);  // file_open_completed
    case 0x18016ee8u:
        return f_18016ee8(cpu);  // slot completion thunk (transfer)
    case 0x180170ccu:
        return f_180170cc(cpu);  // file_close_completed_wrapper
    case 0x18017574u:
        return f_18017574(cpu);  // file_transfer_completed
    case 0x18017a98u:
        return f_18017a98(cpu);  // sound_bank_data_read (as_bank(callback))
    case 0x18017d14u:
        return f_18017d14(cpu);  // sound_bank_header_read (as_bank(callback))
    case 0x18017f00u:
        return f_18017f00(cpu);  // open completion
    case 0x18017f6cu:
        return f_18017f6c(cpu);  // transfer completion
    case 0x180188bcu:
        return f_180188bc(cpu);  // app_noop
    case 0x180188c0u:
        return f_180188c0(cpu);  // app_entry
    case 0x1801891cu:
        return f_1801891c(cpu);  // app_frame
    default:
        fatal("indirect call to %08x, which is not a function the game points at", target);
    }
}

}  // namespace minigolf::game
