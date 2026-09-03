// zip.h — the shared core's, brought into this title's namespace.
//
// The definitions live in `common/src/ipod/gamedata/zip.h` and are compiled once for every
// title. This header exists so that nothing else in this tree has to know that: the include
// path and the qualified names callers already write are unchanged, and the names arrive by
// `using` rather than by alias, so this namespace can still hold what is genuinely this
// title's.
//
// See ../../../common/README.md.
#pragma once

#include "ipod/gamedata/zip.h"

namespace holdem::gamedata {
using ::ipod::gamedata::ZipArchive;
using ::ipod::gamedata::ZipEntry;
}  // namespace holdem::gamedata
