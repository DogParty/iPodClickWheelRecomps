// Sound effects and music.
//
// A sound effect is a handle the game creates, describes (sample rate, channels, bit depth, the
// PCM buffer it loaded) and then starts; music is a named stream the game registers and plays.
// `src/libeapp/audio.cpp` turns both into requests the platform layer performs.
#pragma once

#include "framework/types.h"

#include <cstdint>

namespace vortex::audio {

// --- sound effects -------------------------------------------------------------------------

// Reserve a voice for bank sound `index`; answers its handle, or a full-table error.
[[nodiscard]] uint32_t create_sound(uint32_t enabled, uint32_t index);
void release_sound(uint32_t sound);
void play_sound(uint32_t sound);
void stop_sound(uint32_t sound);

// The PCM the game loaded, and how to read it. Set before the sound is first played.
void set_sound_buffer(uint32_t sound, GuestAddress pcm);
void set_sound_data_size(uint32_t sound, uint32_t bytes);
void set_sound_sample_rate(uint32_t sound, uint32_t hertz);
void set_sound_channels(uint32_t sound, uint32_t channels);
void set_sound_bits(uint32_t sound, uint32_t bits_per_sample);

// Per-playback settings, all 0..255 as the game scales them.
void set_sound_volume(uint32_t sound, uint32_t volume);
void set_sound_pan(uint32_t sound, uint32_t pan);
void set_sound_rate(uint32_t sound, uint32_t rate);

// Two descriptor words the game sets whose meaning the hardware kept to itself: one marks the
// voice ready, the other is written once with zero at set-up.
void set_sound_ready(uint32_t sound, uint32_t ready);
// How many times a play of the sound repeats (Audio #16; the emulator's `SfxRepeat`).
void set_sound_repeat(uint32_t sound, uint32_t count);
void set_sound_reserved(uint32_t sound, uint32_t value);
void set_sound_state(uint32_t sound, uint32_t state);

// Where the voice put the PCM: the game copies its samples there.
[[nodiscard]] GuestAddress sound_data(uint32_t sound);

// --- music ---------------------------------------------------------------------------------

// Register the stream the device most recently resolved a name for; answers its index.
// How many music streams the game has registered so far. The music library reports this as the
// length of the now-playing playlist (src/framework/music_library.h), which is how the game
// learns the index each registration was given.
[[nodiscard]] uint32_t registered_stream_count();

[[nodiscard]] uint32_t register_stream(GuestAddress descriptor, uint32_t reserved, uint32_t flags,
                                       GuestAddress stream);
void play_stream(uint32_t stream);
void set_repeat_mode(uint32_t mode);  // 0 off, 1 one, 2 all
void stop_music();

// The music level, and the scale it is expressed in (the game works in percent and converts).
[[nodiscard]] uint32_t music_level();
[[nodiscard]] uint32_t music_level_scale();
void set_music_level(uint32_t level);

// Three engine queries the game makes at start-up and between tracks. The hardware answered
// each with a constant; the names say what the game does with the answer.
[[nodiscard]] uint32_t engine_ready();     // checked for 1 when deciding whether sound works at all
[[nodiscard]] uint32_t engine_reset();     // called between tracks; the answer is discarded
[[nodiscard]] uint32_t stream_finished();  // polled while a track plays

}  // namespace vortex::audio
