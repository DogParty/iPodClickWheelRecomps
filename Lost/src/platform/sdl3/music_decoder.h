// The music decoder — the shared core's, brought into this title's namespace.
//
// Written in the Mini Golf recomp, which had already moved off `afplay` when this tree had not.
// See ../../../../common/README.md.
#pragma once

#include "ipod/platform/sdl3/music_decoder.h"

namespace lost::platform {
using ::ipod::platform::music_decoding_supported;
using ::ipod::platform::MusicDecoder;
}  // namespace lost::platform
