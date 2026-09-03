// The save store — the shared core's, brought into this title's namespace.
//
// The definitions live in `common/src/ipod/platform/save_store.h` and are compiled once for
// every title. This header exists so that nothing else in this tree has to know that: the include
// path and the qualified names callers already write are unchanged, and `vortex::platform` gains
// the names by `using` rather than by alias, so it can still hold whatever is genuinely this
// title's.
//
// See ../../../common/README.md.
#pragma once

#include "ipod/platform/save_store.h"

namespace vortex::platform {
using ::ipod::platform::make_directory_save_store;
using ::ipod::platform::save_store;
using ::ipod::platform::SaveStore;
using ::ipod::platform::set_save_store;
}  // namespace vortex::platform
