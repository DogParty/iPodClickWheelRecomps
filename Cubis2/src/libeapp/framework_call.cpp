// The call log — the verification oracle — and the ARM entry point the pure recompilation uses.
#include "ipod_eapp.h"
#include "runtime/memory.h"
#include "runtime/runtime.h"

#include <cstring>

namespace cubis::eapp {

namespace {

// The pure recompilation reaches the frameworks through `framework_call`, which records each
// call in ARM form — every argument register, the stack words behind them, and the return
// address — so it can be compared with the emulator's log word for word. When that path is in
// use the typed entry points must not record the same call a second time.
bool arm_form = false;

}  // namespace

void log_call(const char* framework, unsigned ordinal, std::initializer_list<uint32_t> arguments) {
    if (!arm_form && call_log().enabled()) {
        call_log().record(framework, ordinal, arguments);
    }
}

void framework_call(Cpu& cpu, const char* framework, unsigned ordinal,
                    Implementation implementation) {
    arm_form = true;
    if (call_log().enabled()) {
        call_log().record(cpu, framework, ordinal);
    }
    if (implementation != nullptr) {
        implementation(cpu);
    } else {
        cpu.r[0] = 0;  // the emulator's default for an unstubbed import: "return 0"
    }
}

void CallLog::open(const char* path) {
    file_ = std::strcmp(path, "-") == 0 ? stdout : std::fopen(path, "w");
    if (file_ == nullptr) {
        fatal("cannot create call log %s", path);
    }
}

// Format: `FRAME Framework#ordinal a0 a1 a2 a3 a4 a5 a6 a7 from LR`, the emulator's, so the two
// logs can be diffed. The eight words are the call's arguments: the ARM form reports the four
// argument registers and the four stack words behind them, because arguments beyond the fourth
// travel on the stack. `tests/diff.py` compares as many of them as the ordinal really takes.
void CallLog::write(const char* framework, unsigned ordinal, const uint32_t (&arguments)[8],
                    uint32_t return_address) {
    std::fprintf(file_, "%u %s#%u %08x %08x %08x %08x %08x %08x %08x %08x from %08x\n", frame_,
                 framework, ordinal, arguments[0], arguments[1], arguments[2], arguments[3],
                 arguments[4], arguments[5], arguments[6], arguments[7], return_address);
}

void CallLog::record(const Cpu& cpu, const char* framework, unsigned ordinal) {
    const uint32_t sp = cpu.r[SP];
    const uint32_t arguments[8] = {cpu.r[0], cpu.r[1],     cpu.r[2],     cpu.r[3],
                                   ld32(sp), ld32(sp + 4), ld32(sp + 8), ld32(sp + 12)};
    write(framework, ordinal, arguments, cpu.r[LR]);
}

// A typed call knows its arguments exactly. Anything the ordinal does not take is reported as
// zero, and there is no return address to report: the call site is a C++ one.
void CallLog::record(const char* framework, unsigned ordinal,
                     std::initializer_list<uint32_t> given) {
    uint32_t arguments[8] = {};
    unsigned index = 0;
    for (const uint32_t argument : given) {
        if (index == 8) {
            break;
        }
        arguments[index++] = argument;
    }
    write(framework, ordinal, arguments, 0);
}

CallLog::~CallLog() {
    if (file_ != nullptr && file_ != stdout) {
        std::fclose(file_);
    }
}

CallLog& call_log() {
    static CallLog instance;
    return instance;
}

}  // namespace cubis::eapp

// The shared rasteriser (`common/src/ipod/libeapp/gles.cpp`) logs its framework calls
// through `ipod::log_call`. The log is this title's — its ordinals, its recordings — so the
// shared core only declares the name and this is where it is answered.
namespace ipod {
void log_call(const char* framework, unsigned ordinal, std::initializer_list<uint32_t> arguments) {
    ::cubis::eapp::log_call(framework, ordinal, arguments);
}
}  // namespace ipod
