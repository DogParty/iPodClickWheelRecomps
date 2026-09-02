// The Metadata framework: the iPod's music library, answered as an empty one.
//
// See src/framework/music_library.h for what the game asks of it and why the answers are what
// they are. Lost reaches exactly two of the 152 entries it imports, both from one wrapper at
// 0x18006d48; every other entry is left unimplemented, which answers 0 — the library's own
// "nothing there", and a true description of a device with no music on it.
#include "framework/audio.h"
#include "framework/music_library.h"
#include "ipod_eapp.h"
#include "libeapp/heap.h"

namespace holdem {

namespace {

// The playlist block. Its size is the emulator's (reference/eapp-loader/play.rs), and matching
// it matters: it is taken from the same heap the game allocates from, so its size decides where
// the game's own first allocation lands, and the oracle compares those addresses.
constexpr uint32_t PLAYLIST_BLOCK_SIZE = 0x90;

GuestAddress& playlist() {
    static GuestAddress address = 0;
    return address;
}

}  // namespace

namespace music {

void reserve_playlist() {
    playlist() = eapp::heap().alloc(PLAYLIST_BLOCK_SIZE);
}

GuestAddress now_playing_playlist() {
    eapp::log_call("Metadata", 62, {});
    return playlist();
}

uint32_t playlist_track_count() {
    eapp::log_call("Metadata", 134, {});
    return audio::registered_stream_count();
}

}  // namespace music
}  // namespace holdem
