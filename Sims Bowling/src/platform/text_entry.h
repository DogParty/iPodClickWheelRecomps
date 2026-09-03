// Typed text — the shared core's, brought into this title's namespace.
//
// The definitions live in `common/src/ipod/platform/text_entry.h` and are compiled once for
// every title. This header exists so that nothing else in this tree has to know that: the include
// path and the qualified names callers already write are unchanged, and `bowling::platform` gains
// the names by `using` rather than by alias, so it can still hold whatever is genuinely this
// title's.
//
// See ../../../common/README.md.
#pragma once

#include "ipod/platform/text_entry.h"

namespace bowling::platform {
using ::ipod::platform::set_text_entry_supported;
using ::ipod::platform::text_entry_deliver;
using ::ipod::platform::text_entry_supported;
using ::ipod::platform::text_entry_take;
using ::ipod::platform::TypedText;
}  // namespace bowling::platform
