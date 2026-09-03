// Where a Windows build's messages go — the shared core's, brought into this title's namespace.
//
// The definitions live in `common/src/ipod/platform/windows_console.h` and are compiled
// once for every title. This header exists so that nothing else in this tree has to know that:
// the include path and the qualified names callers already write are unchanged, and
// `vortex::platform` gains the name by `using` rather than by alias, so it can still hold whatever
// is genuinely this title's.
//
// See ../../../common/README.md.
#pragma once

#include "ipod/platform/windows_console.h"

namespace vortex::platform {
using ::ipod::platform::windows_console_begin;
}  // namespace vortex::platform
