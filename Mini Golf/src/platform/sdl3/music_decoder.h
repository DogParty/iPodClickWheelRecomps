// The music decoder — the shared core's, brought into this title's namespace.
//
// This tree wrote it, and it is the newer of the two: Lost still spawned `afplay`, which offered
// no volume and no way to stop but a signal. It is compiled once now, from
// `recomps/common/src/ipod/platform/sdl3/music_decoder.cpp`, and both titles use it.
//
// See ../../../../common/README.md.
#pragma once

#include "ipod/platform/sdl3/music_decoder.h"

namespace minigolf::platform {
using ::ipod::platform::music_decoding_supported;
using ::ipod::platform::MusicDecoder;
}  // namespace minigolf::platform
