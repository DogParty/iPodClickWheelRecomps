// The one thing the shared rasteriser needs from a title: somewhere to record that a framework
// call happened.
//
// Every `gfx::` entry point logs its ordinal and arguments, because that log *is* the oracle —
// the recompilation passes when it makes the identical sequence of calls the emulator made. The
// log itself belongs to the title (it knows its own ordinals, its own recordings, its own
// `imports.json`), so only the declaration is here and each title defines it.
#pragma once

#include <cstdint>
#include <initializer_list>

namespace ipod {

void log_call(const char* framework, unsigned ordinal, std::initializer_list<uint32_t> arguments);

}  // namespace ipod
